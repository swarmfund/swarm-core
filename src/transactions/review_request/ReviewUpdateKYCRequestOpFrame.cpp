#include "ReviewUpdateKYCRequestOpFrame.h"
#include "ReviewRequestOpFrame.h"
#include "ReviewRequestHelper.h"
#include "ledger/ReviewableRequestHelper.h"
#include "ledger/AccountHelper.h"
#include "ledger/AccountKYCHelper.h"
#include "transactions/kyc/CreateKYCReviewableRequestOpFrame.h"

namespace stellar {
    ReviewUpdateKYCRequestOpFrame::ReviewUpdateKYCRequestOpFrame(Operation const &op, OperationResult &res,
                                                                 TransactionFrame &parentTx) :
            ReviewRequestOpFrame(op, res, parentTx) {

    }

    SourceDetails
    ReviewUpdateKYCRequestOpFrame::getSourceAccountDetails(
            std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails, int32_t ledgerVersion) const {
        //TODO: replace integer literal with const
        if (mReviewRequest.requestDetails.updateKYC().newTasks & 1 == 0) {
            return SourceDetails({AccountType::MASTER}, mSourceAccount->getHighThreshold(),
                                 static_cast<int32_t>(SignerType::KYC_SUPER_ADMIN));
        }
        return SourceDetails({AccountType::MASTER}, mSourceAccount->getHighThreshold(),
                             static_cast<int32_t>(SignerType::KYC_ACC_MANAGER) |
                             static_cast<int32_t>(SignerType::KYC_SUPER_ADMIN));
    }

    bool
    ReviewUpdateKYCRequestOpFrame::handleApprove(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager,
                                                 ReviewableRequestFrame::pointer request) {
        checkRequestType(request);

        Database &db = ledgerManager.getDatabase();

        auto &updateKYCRequest = request->getRequestEntry().body.updateKYCRequest();

        updateKYCRequest.pendingTasks &= mReviewRequest.requestDetails.updateKYC().newTasks;

        if (updateKYCRequest.pendingTasks != 0) {
            auto &requestEntry = request->getRequestEntry();
            const auto newHash = ReviewableRequestFrame::calculateHash(requestEntry.body);
            requestEntry.hash = newHash;
            ReviewableRequestHelper::Instance()->storeChange(delta, db, request->mEntry);

            innerResult().code(ReviewRequestResultCode::SUCCESS);
            return true;
        }

        EntryHelperProvider::storeDeleteEntry(delta, db, request->getKey());

        createReference(delta, db, request->getRequestor(), request->getReference());

        auto accountToUpdateKYC = updateKYCRequest.accountToUpdateKYC;
        auto accountToUpdateKYCFrame = AccountHelper::Instance()->loadAccount(accountToUpdateKYC, db);
        if (!accountToUpdateKYCFrame) {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state. Requestor account not found.";
            throw std::runtime_error("Unexpected state. Updated account not found.");
        }

        // set KYC Data
        auto kycHelper = AccountKYCHelper::Instance();
        auto updatedKYC = kycHelper->loadAccountKYC(accountToUpdateKYC, db, &delta);
        if (!updatedKYC) {
            auto updatedKYCAccountFrame = AccountKYCFrame::createNew(accountToUpdateKYC, updateKYCRequest.kycData);
            kycHelper->storeAdd(delta, db, updatedKYCAccountFrame->mEntry);
        } else {
            updatedKYC->setKYCData(updateKYCRequest.kycData);
            kycHelper->storeChange(delta, db, updatedKYC->mEntry);
        }

        auto &accountEntry = accountToUpdateKYCFrame->getAccount();
        accountEntry.ext.v(LedgerVersion::USE_KYC_LEVEL);
        accountToUpdateKYCFrame->setKYCLevel(updateKYCRequest.kycLevel);
        accountToUpdateKYCFrame->setAccountType(updateKYCRequest.accountTypeToSet);
        EntryHelperProvider::storeChangeEntry(delta, db, accountToUpdateKYCFrame->mEntry);

        innerResult().code(ReviewRequestResultCode::SUCCESS);
        return true;
    }

    bool ReviewUpdateKYCRequestOpFrame::handleReject(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager,
                                                     ReviewableRequestFrame::pointer request) {
        checkRequestType(request);

        Database &db = ledgerManager.getDatabase();

        auto &updateKYCRequest = request->getRequestEntry().body.updateKYCRequest();

        updateKYCRequest.allTasks = CreateUpdateKYCRequestOpFrame::defaultTasks;
        updateKYCRequest.pendingTasks = updateKYCRequest.allTasks;
        updateKYCRequest.sequenceNumber++;
        updateKYCRequest.externalDetails.emplace_back(mReviewRequest.requestDetails.updateKYC().externalDetails);

        request->setRejectReason(mReviewRequest.reason);

        auto &requestEntry = request->getRequestEntry();
        const auto newHash = ReviewableRequestFrame::calculateHash(requestEntry.body);
        requestEntry.hash = newHash;
        ReviewableRequestHelper::Instance()->storeChange(delta, db, request->mEntry);

        innerResult().code(ReviewRequestResultCode::SUCCESS);
        return true;
    }

    void ReviewUpdateKYCRequestOpFrame::checkRequestType(ReviewableRequestFrame::pointer request) {
        if (request->getRequestType() != ReviewableRequestType::UPDATE_KYC) {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected request type. Expected UPDATE_KYC, but got " << xdr::
            xdr_traits<ReviewableRequestType>::
            enum_name(request->getRequestType());
            throw std::invalid_argument("Unexpected request type for review update KYC request");
        }
    }

    bool ReviewUpdateKYCRequestOpFrame::handlePermanentReject(Application &app, LedgerDelta &delta,
                                                              LedgerManager &ledgerManager,
                                                              ReviewableRequestFrame::pointer request) {
        innerResult().code(ReviewRequestResultCode::PERMANENT_REJECT_NOT_ALLOWED);
        return false;
    }

    bool ReviewUpdateKYCRequestOpFrame::doCheckValid(Application &app) {
        std::string externalDetails = mReviewRequest.requestDetails.updateKYC().externalDetails;
        if (!isValidJson(externalDetails)) {
            innerResult().code(ReviewRequestResultCode::INVALID_EXTERNAL_DETAILS);
            return false;
        }

        return true;
    }
}