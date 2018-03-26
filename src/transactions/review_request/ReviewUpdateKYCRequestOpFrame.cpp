#include "ReviewUpdateKYCRequestOpFrame.h"
#include "ReviewRequestOpFrame.h"
#include "ReviewRequestHelper.h"
#include "ledger/ReviewableRequestHelper.h"
#include "ledger/AccountHelper.h"
#include "ledger/AccountKYCHelper.h"
#include "transactions/CreateKYCReviewableRequestOpFrame.h"

namespace stellar {
    ReviewUpdateKYCRequestOpFrame::ReviewUpdateKYCRequestOpFrame(Operation const &op, OperationResult &res,
                                                                 TransactionFrame &parentTx) :
            ReviewRequestOpFrame(op, res, parentTx) {

    }

    SourceDetails
    ReviewUpdateKYCRequestOpFrame::getSourceAccountDetails(
            std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
            int32_t ledgerVersion) const {
        return SourceDetails({AccountType::MASTER}, mSourceAccount->getHighThreshold(),
                             static_cast<int32_t>(SignerType::KYC_ACC_MANAGER) |
                             static_cast<int32_t>(SignerType::KYC_SUPER_ADMIN));
    }

    //TODO: edit handleApprove
    bool
    ReviewUpdateKYCRequestOpFrame::handleApprove(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager,
                                                 ReviewableRequestFrame::pointer request) {
        checkRequestType(request);

        Database &db = ledgerManager.getDatabase();
        EntryHelperProvider::storeDeleteEntry(delta, db, request->getKey());

        auto changeKYCRequest = request->getRequestEntry().body.updateKYCRequest();


        auto updatedAccountID = changeKYCRequest.accountToUpdateKYC;
        auto updatedAccountFrame = AccountHelper::Instance()->loadAccount(updatedAccountID, db);
        if (!updatedAccountFrame) {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state. Requestor account not found.";
            throw std::runtime_error("Unexpected state. Updated account not found.");
        }


        // set KYC Data
        auto kycHelper = AccountKYCHelper::Instance();
        auto updatedKYC = kycHelper->loadAccountKYC(updatedAccountID, db, &delta);
        if (!updatedKYC) {
            auto updatedKYCAccountFrame = AccountKYCFrame::createNew(updatedAccountID, changeKYCRequest.kycData);
            kycHelper->storeAdd(delta, db, updatedKYCAccountFrame->mEntry);
        } else {
            updatedKYC->setKYCData(changeKYCRequest.kycData);
            kycHelper->storeChange(delta, db, updatedKYC->mEntry);
        }

        auto &accountEntry = updatedAccountFrame->getAccount();
        accountEntry.ext.v(LedgerVersion::USE_KYC_LEVEL);
        updatedAccountFrame->setKYCLevel(changeKYCRequest.kycLevel);
        updatedAccountFrame->setAccountType(changeKYCRequest.accountTypeToSet);
        EntryHelperProvider::storeChangeEntry(delta, db, updatedAccountFrame->mEntry);

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
        updateKYCRequest.externalDetails.emplace_back(mReviewRequest.reason);

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
}