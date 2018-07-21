// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <transactions/manage_asset/ManageAssetHelper.h>
#include "util/asio.h"
#include "ReviewInvoiceRequestOpFrame.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "ledger/AssetHelper.h"
#include "ledger/BalanceHelper.h"
#include "main/Application.h"

namespace stellar
{

using namespace std;
using xdr::operator==;


SourceDetails
ReviewInvoiceRequestOpFrame::getSourceAccountDetails(
        unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails, int32_t ledgerVersion) const
{
    std::vector<AccountType> allowedAccountTypes = {AccountType::GENERAL, AccountType::NOT_VERIFIED,
                                                    AccountType::EXCHANGE, AccountType::ACCREDITED_INVESTOR,
                                                    AccountType::INSTITUTIONAL_INVESTOR, AccountType::VERIFIED,
                                                    AccountType::MASTER};

    return SourceDetails(allowedAccountTypes, mSourceAccount->getHighThreshold(),
                         static_cast<int32_t>(SignerType::INVOICE_MANAGER));
}

bool
ReviewInvoiceRequestOpFrame::handleApprove(Application& app, LedgerDelta& delta,
                                           LedgerManager& ledgerManager,
                                           ReviewableRequestFrame::pointer request)
{
    innerResult().code(ReviewRequestResultCode::SUCCESS);

    if (request->getRequestType() != ReviewableRequestType::INVOICE)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected request type. Expected INVOICE, but got "
                               << xdr::xdr_traits<ReviewableRequestType>::enum_name(request->getRequestType());
        throw invalid_argument("Unexpected request type for review invoice request");
    }

    auto& invoiceRequest = request->getRequestEntry().body.invoiceRequest();

    if (!(invoiceRequest.sender == getSourceID()))
    {
        innerResult().code(ReviewRequestResultCode::ONLY_SENDER_CAN_APPROVE_INVOICE);
        return false;
    }

    if (invoiceRequest.isSecured)
    {
        innerResult().code(ReviewRequestResultCode::ALREADY_APPROVED);
        return false;
    }

    Database& db = ledgerManager.getDatabase();

    auto senderBalance = BalanceHelper::Instance()->loadBalance(invoiceRequest.sender,
                                                                invoiceRequest.asset, db, &delta);
    if (!senderBalance)
    {
        innerResult().code(ReviewRequestResultCode::BALANCE_NOT_FOUND);
        return false;
    }

    if (!tryLockAmount(senderBalance, invoiceRequest.amount))
        return false;

    invoiceRequest.isSecured = true;
    request->recalculateHashRejectReason();
    EntryHelperProvider::storeChangeEntry(delta, db, request->mEntry);
    EntryHelperProvider::storeChangeEntry(delta, db, senderBalance->mEntry);

    return true;
}

bool
ReviewInvoiceRequestOpFrame::tryLockAmount(BalanceFrame::pointer balance, uint64_t amount)
{
    auto lockResult = balance->tryLock(amount);

    switch (lockResult)
    {
        case BalanceFrame::SUCCESS:
        {
            return true;
        }
        case BalanceFrame::LINE_FULL:
        {
            innerResult().code(ReviewRequestResultCode::LOCKED_AMOUNT_OVERFLOW);
            return false;
        }
        case BalanceFrame::UNDERFUNDED:
        {
            innerResult().code(ReviewRequestResultCode::BALANCE_UNDERFUNDED);
            return false;
        }
        default:
        {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected result code from tryLock method: "
                                                   << static_cast<int>(lockResult);
            throw std::runtime_error("Unexpected result code from tryLock method");
        }
    }
}

bool
ReviewInvoiceRequestOpFrame::handleReject(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager,
                                          ReviewableRequestFrame::pointer request)
{
    innerResult().code(ReviewRequestResultCode::REJECT_NOT_ALLOWED);
    return false;
}

bool
ReviewInvoiceRequestOpFrame::handlePermanentReject(Application& app, LedgerDelta& delta,
                                                   LedgerManager& ledgerManager,
                                                   ReviewableRequestFrame::pointer request)
{
    if (request->getRequestType() != ReviewableRequestType::INVOICE)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected request type. Expected INVOICE, but got "
                             << xdr::xdr_traits<ReviewableRequestType>::enum_name(request->getRequestType());
        throw invalid_argument("Unexpected request type for review invoice request");
    }

    if (request->getRequestEntry().body.invoiceRequest().isSecured)
    {
        innerResult().code(ReviewRequestResultCode::PERMANENT_REJECT_NOT_ALLOWED);
        return false;
    }

    return ReviewRequestOpFrame::handlePermanentReject(app, delta, ledgerManager, request);
}

ReviewInvoiceRequestOpFrame::ReviewInvoiceRequestOpFrame(Operation const & op, OperationResult & res,
                                                         TransactionFrame & parentTx) :
        ReviewRequestOpFrame(op, res, parentTx)
{
}

}
