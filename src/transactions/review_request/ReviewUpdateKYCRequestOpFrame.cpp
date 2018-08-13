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
        int32_t superAdminTask = mReviewRequest.requestDetails.updateKYC().tasksToRemove & 1;
        if (superAdminTask == 1) {
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
        CreateUpdateKYCRequestOpFrame::checkRequestType(request);

        Database &db = ledgerManager.getDatabase();

        auto &updateKYCRequest = request->getRequestEntry().body.updateKYCRequest();

        updateKYCRequest.allTasks |= mReviewRequest.requestDetails.updateKYC().tasksToAdd;
        updateKYCRequest.pendingTasks &= ~mReviewRequest.requestDetails.updateKYC().tasksToRemove;
        updateKYCRequest.pendingTasks |= mReviewRequest.requestDetails.updateKYC().tasksToAdd;
        updateKYCRequest.externalDetails.emplace_back(mReviewRequest.requestDetails.updateKYC().externalDetails);

        auto &requestEntry = request->getRequestEntry();
        const auto newHash = ReviewableRequestFrame::calculateHash(requestEntry.body);
        requestEntry.hash = newHash;

        ReviewableRequestHelper::Instance()->storeChange(delta, db, request->mEntry);

        if (!canBeFulfilled(requestEntry)) {
            innerResult().code(ReviewRequestResultCode::SUCCESS);
            return true;
        }

        EntryHelperProvider::storeDeleteEntry(delta, db, request->getKey());

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
        if (mReviewRequest.requestDetails.updateKYC().tasksToRemove != 0) {
            if (ledgerManager.shouldUse(LedgerVersion::ERROR_ON_NON_ZERO_TASKS_TO_REMOVE_IN_REJECT_KYC)) {
                innerResult().code(ReviewRequestResultCode::NON_ZERO_TASKS_TO_REMOVE_NOT_ALLOWED);
                return false;
            }

            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state. Tasks to remove must be zero.";
            throw std::runtime_error("Unexpected state. Tasks to remove must be zero.");
        }

        CreateUpdateKYCRequestOpFrame::checkRequestType(request);

        Database &db = ledgerManager.getDatabase();

        auto &updateKYCRequest = request->getRequestEntry().body.updateKYCRequest();

        updateKYCRequest.allTasks |= mReviewRequest.requestDetails.updateKYC().tasksToAdd;
        updateKYCRequest.pendingTasks = updateKYCRequest.allTasks;
        updateKYCRequest.externalDetails.emplace_back(mReviewRequest.requestDetails.updateKYC().externalDetails);

        request->setRejectReason(mReviewRequest.reason);

        auto &requestEntry = request->getRequestEntry();
        const auto newHash = ReviewableRequestFrame::calculateHash(requestEntry.body);
        requestEntry.hash = newHash;
        ReviewableRequestHelper::Instance()->storeChange(delta, db, request->mEntry);

        innerResult().code(ReviewRequestResultCode::SUCCESS);
        return true;
    }

    bool ReviewUpdateKYCRequestOpFrame::canBeFulfilled(ReviewableRequestEntry &requestEntry) {
        return requestEntry.body.updateKYCRequest().pendingTasks == 0;
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