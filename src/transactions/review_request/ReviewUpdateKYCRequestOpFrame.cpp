//
// Created by volodymyr on 02.01.18.
//

#include <ledger/EntryHelper.h>
#include <ledger/AccountKYCHelper.h>
#include <ledger/AccountHelper.h>
#include "ReviewUpdateKYCRequestOpFrame.h"

namespace stellar
{

ReviewUpdateKYCRequestOpFrame::ReviewUpdateKYCRequestOpFrame(const Operation &op,
                                                             OperationResult &opRes,
                                                             TransactionFrame &parentTx)
        : ReviewRequestOpFrame(op, opRes, parentTx)
{
}

SourceDetails ReviewUpdateKYCRequestOpFrame::getSourceAccountDetails(
        std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const
{
    return SourceDetails({AccountType::MASTER}, mSourceAccount->getHighThreshold(),
                         static_cast<int32_t>(SignerType::ACCOUNT_MANAGER));
}

bool ReviewUpdateKYCRequestOpFrame::handleApprove(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager,
                                             ReviewableRequestFrame::pointer request)
{
    if (request->getRequestType() != ReviewableRequestType::UPDATE_KYC)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected request type. Expected UPDATE_KYC, but got " << xdr::
                                               xdr_traits<ReviewableRequestType>::
                                               enum_name(request->getRequestType());
        throw std::invalid_argument("Unexpected request type for review update KYC request");
    }

    Database& db = ledgerManager.getDatabase();
    EntryHelperProvider::storeDeleteEntry(delta, db, request->getKey());

    auto updateKYCRequest = request->getRequestEntry().body.updateKYCRequest();

    AccountID requestorID = request->getRequestor();
    auto requestorAccount = AccountHelper::Instance()->loadAccount(requestorID, db);
    if (!requestorAccount)
    {
        innerResult().code(ReviewRequestResultCode::REQUESTOR_NOT_FOUND);
        return false;
    }
    // change requestor's account type back to the original state
    requestorAccount->setAccountType(updateKYCRequest.accountType);
    EntryHelperProvider::storeChangeEntry(delta, db, requestorAccount->mEntry);

    //update KYC data of the account
    auto accountKYC = AccountKYCFrame::createNew(requestorID, updateKYCRequest.dataKYC);
    EntryHelperProvider::storeAddOrChangeEntry(delta, db, accountKYC->mEntry);

    innerResult().code(ReviewRequestResultCode::SUCCESS);
    return true;
}

bool ReviewUpdateKYCRequestOpFrame::handlePermanentReject(Application &app, LedgerDelta &delta,
                                                          LedgerManager &ledgerManager,
                                                          ReviewableRequestFrame::pointer request)
{
    innerResult().code(ReviewRequestResultCode::PERMANENT_REJECT_NOT_ALLOWED);
    return false;
}


}


