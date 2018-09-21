#include <transactions/ManageKeyValueOpFrame.h>
#include "CreateKYCReviewableRequestOpFrame.h"
#include "ledger/AccountHelper.h"
#include "main/Application.h"

#include "bucket/BucketApplicator.h"
#include "xdrpp/printer.h"
#include "ledger/LedgerDelta.h"
#include "transactions/review_request/ReviewRequestHelper.h"
#include "transactions/review_request/ReviewUpdateKYCRequestOpFrame.h"
#include "ledger/KeyValueHelperLegacy.h"

namespace stellar {
    using namespace std;
    using xdr::operator==;

    uint32 const CreateUpdateKYCRequestOpFrame::defaultTasks = 30;

    CreateUpdateKYCRequestOpFrame::CreateUpdateKYCRequestOpFrame(Operation const &op, OperationResult &res,
                                                                 TransactionFrame &parentTx)
            : OperationFrame(op, res, parentTx), mCreateUpdateKYCRequest(mOperation.body.createUpdateKYCRequestOp()) {

    }

    std::unordered_map<AccountID, CounterpartyDetails>
    CreateUpdateKYCRequestOpFrame::getCounterpartyDetails(Database &db, LedgerDelta *delta) const {
        throw std::runtime_error("Get counterparty details for CreateUpdateKYCRequestOpFrame is not implemented");
    }

    std::unordered_map<AccountID, CounterpartyDetails>
    CreateUpdateKYCRequestOpFrame::getCounterpartyDetails(Database &db, LedgerDelta *delta, int32_t ledgerVersion) const {
        std::vector<AccountType> allowedAccountTypes = {AccountType::GENERAL, AccountType::NOT_VERIFIED,
                                                        AccountType::ACCREDITED_INVESTOR,
                                                        AccountType::INSTITUTIONAL_INVESTOR,
                                                        AccountType::VERIFIED};
        if (ledgerVersion >= static_cast<int32_t>(LedgerVersion::ALLOW_SYNDICATE_TO_UPDATE_KYC)) {
            allowedAccountTypes.push_back(AccountType::SYNDICATE);
        }
        return {{mCreateUpdateKYCRequest.updateKYCRequestData.accountToUpdateKYC,
                        CounterpartyDetails(allowedAccountTypes, true, true)}
        };
    }

    SourceDetails
    CreateUpdateKYCRequestOpFrame::getSourceAccountDetails(
            std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
            int32_t ledgerVersion) const {
        if (!!mCreateUpdateKYCRequest.updateKYCRequestData.allTasks) {
            return SourceDetails({AccountType::MASTER}, mSourceAccount->getHighThreshold(),
                                 static_cast<int32_t>(SignerType::KYC_SUPER_ADMIN));
        }
        if (getSourceID() == mCreateUpdateKYCRequest.updateKYCRequestData.accountToUpdateKYC) {
            int32_t allowedSignerTypes = static_cast<int32_t>(SignerType::KYC_ACC_MANAGER);
            if (ledgerVersion >= static_cast<int32_t>(LedgerVersion::ALLOW_ACCOUNT_MANAGER_TO_CHANGE_KYC))
            {
                allowedSignerTypes |= static_cast<int32_t>(SignerType::ACCOUNT_MANAGER);
            }

            std::vector<AccountType> allowedAccountTypes = {AccountType::GENERAL, AccountType::NOT_VERIFIED,
                                                            AccountType::ACCREDITED_INVESTOR,
                                                            AccountType::INSTITUTIONAL_INVESTOR,
                                                            AccountType::VERIFIED};

            if (ledgerVersion >= static_cast<int32_t>(LedgerVersion::ALLOW_SYNDICATE_TO_UPDATE_KYC))
            {
                allowedAccountTypes.push_back(AccountType::SYNDICATE);
            }

            return SourceDetails(allowedAccountTypes, mSourceAccount->getHighThreshold(), allowedSignerTypes);
        }
        return SourceDetails({AccountType::MASTER}, mSourceAccount->getHighThreshold(),
                             static_cast<int32_t>(SignerType::KYC_ACC_MANAGER) |
                             static_cast<int32_t>(SignerType::KYC_SUPER_ADMIN));
    }

    bool CreateUpdateKYCRequestOpFrame::ensureUpdateKYCDataValid(ReviewableRequestEntry &requestEntry) {
        auto &updateKYCRequest = requestEntry.body.updateKYCRequest();
        auto updateKYCRequestData = mCreateUpdateKYCRequest.updateKYCRequestData;

        if (!(updateKYCRequest.accountToUpdateKYC == updateKYCRequestData.accountToUpdateKYC)) {
            return false;
        }
        if (updateKYCRequest.accountTypeToSet != updateKYCRequestData.accountTypeToSet) {
            return false;
        }
        if (updateKYCRequest.kycLevel != updateKYCRequestData.kycLevelToSet) {
            return false;
        }
        if (!!updateKYCRequestData.allTasks) {
            return false;
        }

        return true;
    }

    bool CreateUpdateKYCRequestOpFrame::changeUpdateKYCRequest(Database &db, LedgerDelta &delta, Application &app) {
        if (mSourceAccount->getAccountType() == AccountType::MASTER) {
            innerResult().code(CreateUpdateKYCRequestResultCode::NOT_ALLOWED_TO_UPDATE_REQUEST);
            return false;
        }

        auto request = ReviewableRequestHelper::Instance()->loadRequest(mCreateUpdateKYCRequest.requestID, db, &delta);
        if (!request) {
            innerResult().code(CreateUpdateKYCRequestResultCode::REQUEST_DOES_NOT_EXIST);
            return false;
        }
        if (request->getRejectReason().empty()) {
            innerResult().code(CreateUpdateKYCRequestResultCode::PENDING_REQUEST_UPDATE_NOT_ALLOWED);
            return false;
        }

        auto &requestEntry = request->getRequestEntry();
        if (!ensureUpdateKYCDataValid(requestEntry)) {
            innerResult().code(CreateUpdateKYCRequestResultCode::INVALID_UPDATE_KYC_REQUEST_DATA);
            return false;
        }

        updateRequest(requestEntry);

        request->recalculateHashRejectReason();

        ReviewableRequestHelper::Instance()->storeChange(delta, db, request->mEntry);

        innerResult().code(CreateUpdateKYCRequestResultCode::SUCCESS);
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

        uint32 defaultMask;

        if(!getDefaultKYCMask(db, ledgerManager, mCreateUpdateKYCRequest.updateKYCRequestData,
                                                    accountFrame, defaultMask))
        {
            innerResult().code(CreateUpdateKYCRequestResultCode::KYC_RULE_NOT_FOUND);
            return false;
        }

        createRequest(requestEntry, defaultMask);

        requestFrame->recalculateHashRejectReason();

        requestHelper->storeAdd(delta, db, requestFrame->mEntry);

        innerResult().code(CreateUpdateKYCRequestResultCode::SUCCESS);
        innerResult().success().requestID = requestFrame->getRequestID();
        innerResult().success().fulfilled = false;

        bool canAutoApprove = ReviewUpdateKYCRequestOpFrame::canBeFulfilled(requestEntry);

        if (!ledgerManager.shouldUse(LedgerVersion::FIX_CREATE_KYC_REQUEST_AUTO_APPROVE))
            canAutoApprove = canAutoApprove &&
                             mSourceAccount->getAccountType() == AccountType::MASTER;

        if (canAutoApprove)
            tryAutoApprove(db, delta, app, requestFrame);

        return true;
    }

    void
    CreateUpdateKYCRequestOpFrame::tryAutoApprove(Database &db, LedgerDelta &delta, Application &app,
                                                  ReviewableRequestFrame::pointer requestFrame) {
        auto &ledgerManager = app.getLedgerManager();
        auto result = ReviewRequestHelper::tryApproveRequest(mParentTx, app, ledgerManager, delta, requestFrame);
        if (result != ReviewRequestResultCode::SUCCESS) {
            CLOG(ERROR, Logging::OPERATION_LOGGER)
                    << "Unexpected state: tryApproveRequest expected to be success, but was: "
                    << xdr::xdr_to_string(result);
            throw std::runtime_error("Unexpected state: tryApproveRequest expected to be success");
        }

        innerResult().success().fulfilled = true;
    }

    bool CreateUpdateKYCRequestOpFrame::doCheckValid(Application &app) {
        std::string kycData = mCreateUpdateKYCRequest.updateKYCRequestData.kycData;
        if (!isValidJson(kycData)) {
            innerResult().code(CreateUpdateKYCRequestResultCode::INVALID_KYC_DATA);
            return false;
        }
        return true;
    }

    std::string CreateUpdateKYCRequestOpFrame::getReference() const {
        const auto hash = sha256(xdr::xdr_to_opaque(ReviewableRequestType::UPDATE_KYC));
        return binToHex(hash);
    }

    void
    CreateUpdateKYCRequestOpFrame::createRequest(ReviewableRequestEntry &requestEntry, uint32 defaultMask) {
        requestEntry.body.updateKYCRequest().accountToUpdateKYC = mCreateUpdateKYCRequest.updateKYCRequestData.accountToUpdateKYC;
        requestEntry.body.updateKYCRequest().accountTypeToSet = mCreateUpdateKYCRequest.updateKYCRequestData.accountTypeToSet;
        requestEntry.body.updateKYCRequest().kycLevel = mCreateUpdateKYCRequest.updateKYCRequestData.kycLevelToSet;
        requestEntry.body.updateKYCRequest().kycData = mCreateUpdateKYCRequest.updateKYCRequestData.kycData;

        requestEntry.body.updateKYCRequest().allTasks = !!mCreateUpdateKYCRequest.updateKYCRequestData.allTasks
                                                        ?mCreateUpdateKYCRequest.updateKYCRequestData.allTasks.activate()
                                                        :defaultMask;

        requestEntry.body.updateKYCRequest().pendingTasks = requestEntry.body.updateKYCRequest().allTasks;
        requestEntry.body.updateKYCRequest().sequenceNumber = 0;
    }

    bool
    CreateUpdateKYCRequestOpFrame::getDefaultKYCMask(Database &db, LedgerManager &ledgerManager,
                                                     UpdateKYCRequestData kycRequestData,
                                                     AccountFrame::pointer account, uint32 &defaultMask)
    {
        if(!ledgerManager.shouldUse(LedgerVersion::KYC_RULES))
        {
            defaultMask = CreateUpdateKYCRequestOpFrame::defaultTasks;
            return true;
        }

        auto  key = ManageKeyValueOpFrame::makeKYCRuleKey(account->getAccount().accountType,account->getKYCLevel(),
                                                          kycRequestData.accountTypeToSet,kycRequestData.kycLevelToSet);

        auto kvEntry = KeyValueHelperLegacy::Instance()->loadKeyValue(key,db);

        if (!kvEntry)
        {
            return false;
        }

        if (kvEntry.get()->getKeyValue().value.type() != KeyValueEntryType::UINT32){
            throw std::runtime_error("Unexpected database state, expected kyc rule to be UINT32");
        }

        defaultMask = kvEntry.get()->getKeyValue().value.ui32Value();

        return true;
    }

    void CreateUpdateKYCRequestOpFrame::updateRequest(ReviewableRequestEntry &requestEntry) {
        requestEntry.body.updateKYCRequest().kycData = mCreateUpdateKYCRequest.updateKYCRequestData.kycData;
        requestEntry.body.updateKYCRequest().pendingTasks = requestEntry.body.updateKYCRequest().allTasks;
        requestEntry.body.updateKYCRequest().sequenceNumber++;
    }
}