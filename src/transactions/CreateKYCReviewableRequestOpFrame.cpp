#include "CreateKYCReviewableRequestOpFrame.h"
#include "database/Database.h"
#include "ledger/AccountHelper.h"
#include "main/Application.h"

#include "bucket/BucketApplicator.h"
#include "xdrpp/printer.h"
#include "ledger/LedgerDelta.h"
#include "review_request/ReviewRequestHelper.h"

namespace stellar {
    using namespace std;
    using xdr::operator==;

    uint32 const CreateUpdateKYCRequestOpFrame::defaultTasks = 3;

    CreateUpdateKYCRequestOpFrame::CreateUpdateKYCRequestOpFrame(Operation const &op, OperationResult &res,
                                                                 TransactionFrame &parentTx)
            : OperationFrame(op, res, parentTx), mCreateUpdateKYCRequest(mOperation.body.createUpdateKYCRequestOp()) {

    }

    std::unordered_map<AccountID, CounterpartyDetails>
    CreateUpdateKYCRequestOpFrame::getCounterpartyDetails(Database &db, LedgerDelta *delta) const {
        return {{mCreateUpdateKYCRequest.updateKYCRequestData.accountToUpdateKYC,
                        CounterpartyDetails({AccountType::GENERAL, AccountType::NOT_VERIFIED}, true, true)}
        };
    }

    SourceDetails
    CreateUpdateKYCRequestOpFrame::getSourceAccountDetails(
            std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
            int32_t ledgerVersion) const {
        if (mCreateUpdateKYCRequest.updateKYCRequestData.allTasks) {
            return SourceDetails({AccountType::MASTER}, mSourceAccount->getHighThreshold(),
                                 static_cast<int32_t>(SignerType::KYC_SUPER_ADMIN));
        }
        if (getSourceID() == mCreateUpdateKYCRequest.updateKYCRequestData.accountToUpdateKYC) {
            return SourceDetails({AccountType::GENERAL, AccountType::NOT_VERIFIED},
                                 mSourceAccount->getHighThreshold(), static_cast<int32_t>(SignerType::KYC_ACC_MANAGER));
        }
        return SourceDetails({AccountType::MASTER}, mSourceAccount->getHighThreshold(),
                             static_cast<int32_t>(SignerType::KYC_ACC_MANAGER));
    }

    bool CreateUpdateKYCRequestOpFrame::changeUpdateKYCRequest(Database &db, LedgerDelta &delta, Application &app) {
        innerResult().code(CreateUpdateKYCRequestResultCode::SUCCESS);

        auto requestHelper = ReviewableRequestHelper::Instance();
        auto request = requestHelper->loadRequest(mCreateUpdateKYCRequest.requestID,
                                                  mCreateUpdateKYCRequest.updateKYCRequestData.accountToUpdateKYC,
                                                  ReviewableRequestType::UPDATE_KYC, db, &delta);
        if (!request) {
            innerResult().code(CreateUpdateKYCRequestResultCode::REQUEST_DOES_NOT_EXIST);
            return false;
        }

        if (mSourceAccount->getAccountType() != AccountType::MASTER && request->getRejectReason().empty()) {
            innerResult().code(CreateUpdateKYCRequestResultCode::PENDING_REQUEST_UPDATE_NOT_ALLOWED);
            return false;
        }

        if (tryAutoApprove(db, delta, app, request)) {
            innerResult().success().requestID = mCreateUpdateKYCRequest.requestID;
            return true;
        }

        auto &requestEntry = request->getRequestEntry();
        createRequest(requestEntry);
        const auto newHash = ReviewableRequestFrame::calculateHash(requestEntry.body);
        requestEntry.hash = newHash;
        ReviewableRequestHelper::Instance()->storeChange(delta, db, request->mEntry);

        innerResult().success().requestID = mCreateUpdateKYCRequest.requestID;
        return true;
    }

    bool CreateUpdateKYCRequestOpFrame::doApply(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager) {
        Database &db = ledgerManager.getDatabase();

        if (mCreateUpdateKYCRequest.requestID != 0) {
            return changeUpdateKYCRequest(db, delta, app);
        }

        auto accountHelper = AccountHelper::Instance();
        auto accountFrame = accountHelper->loadAccount(delta,
                                                       mCreateUpdateKYCRequest.updateKYCRequestData.accountToUpdateKYC,
                                                       db);
        if (!accountFrame) {
            innerResult().code(CreateUpdateKYCRequestResultCode::ACC_TO_UPDATE_DOES_NOT_EXIST);
            return false;
        }

        auto updateKYCRequestData = mCreateUpdateKYCRequest.updateKYCRequestData;
        auto account = accountFrame->getAccount();
        if (account.accountType == updateKYCRequestData.accountTypeToSet &&
            accountFrame->getKYCLevel() == updateKYCRequestData.kycLevel) {
            innerResult().code(CreateUpdateKYCRequestResultCode::SAME_ACC_TYPE_TO_SET);
            return false;
        }

        auto reference = getReference();
        const auto referencePtr = xdr::pointer<string64>(new string64(reference));
        auto requestFrame = ReviewableRequestFrame::createNew(delta, updateKYCRequestData.accountToUpdateKYC,
                                                              app.getMasterID(),
                                                              referencePtr, ledgerManager.getCloseTime());

        auto requestHelper = ReviewableRequestHelper::Instance();
        if (requestHelper->isReferenceExist(db, updateKYCRequestData.accountToUpdateKYC, reference,
                                            requestFrame->getRequestID())) {
            innerResult().code(CreateUpdateKYCRequestResultCode::REQUEST_ALREADY_EXISTS);
            return false;
        }

        auto &requestEntry = requestFrame->getRequestEntry();
        requestEntry.body.type(ReviewableRequestType::UPDATE_KYC);
        createRequest(requestEntry);

        requestFrame->recalculateHashRejectReason();

        requestHelper->storeAdd(delta, db, requestFrame->mEntry);

        innerResult().code(CreateUpdateKYCRequestResultCode::SUCCESS);
        innerResult().success().requestID = requestFrame->getRequestID();
        innerResult().success().fulfilled = false;

        tryAutoApprove(db, delta, app, requestFrame);

        return true;
    }

    bool
    CreateUpdateKYCRequestOpFrame::tryAutoApprove(Database &db, LedgerDelta &delta, Application &app,
                                                  ReviewableRequestFrame::pointer requestFrame) {
        if (!mCreateUpdateKYCRequest.updateKYCRequestData.allTasks) {
            return false;
        }

        if (!mCreateUpdateKYCRequest.updateKYCRequestData.allTasks.activate() == 0) {
            return false;
        }

        auto &ledgerManager = app.getLedgerManager();
        auto result = ReviewRequestHelper::tryApproveRequest(mParentTx, app, ledgerManager, delta, requestFrame);
        if (result != ReviewRequestResultCode::SUCCESS) {
            CLOG(ERROR, Logging::OPERATION_LOGGER)
                    << "Unexpected state: tryApproveRequest expected to be success, but was: "
                    << xdr::xdr_to_string(result);
            throw std::runtime_error("Unexpected state: tryApproveRequest expected to be success");
        }
        innerResult().success().fulfilled = true;
        return true;
    }

    bool CreateUpdateKYCRequestOpFrame::doCheckValid(Application &app) {
        return true;
    }

    std::string CreateUpdateKYCRequestOpFrame::getReference() const {
        const auto hash = sha256(xdr::xdr_to_opaque(ReviewableRequestType::UPDATE_KYC));
        return binToHex(hash);
    }

    void CreateUpdateKYCRequestOpFrame::createRequest(ReviewableRequestEntry &requestEntry) {
        requestEntry.body.updateKYCRequest().accountToUpdateKYC = mCreateUpdateKYCRequest.updateKYCRequestData.accountToUpdateKYC;
        requestEntry.body.updateKYCRequest().accountTypeToSet = mCreateUpdateKYCRequest.updateKYCRequestData.accountTypeToSet;
        requestEntry.body.updateKYCRequest().kycLevel = mCreateUpdateKYCRequest.updateKYCRequestData.kycLevel;
        requestEntry.body.updateKYCRequest().kycData = mCreateUpdateKYCRequest.updateKYCRequestData.kycData;

        requestEntry.body.updateKYCRequest().allTasks = mCreateUpdateKYCRequest.updateKYCRequestData.allTasks
                                                        ? mCreateUpdateKYCRequest.updateKYCRequestData.allTasks.activate()
                                                        : CreateUpdateKYCRequestOpFrame::defaultTasks;

        requestEntry.body.updateKYCRequest().pendingTasks = requestEntry.body.updateKYCRequest().allTasks;

        if (requestEntry.rejectReason.empty()) {
            requestEntry.body.updateKYCRequest().sequenceNumber = 0;
        }
    }
}