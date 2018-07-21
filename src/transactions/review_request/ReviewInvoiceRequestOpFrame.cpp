// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <transactions/manage_asset/ManageAssetHelper.h>
#include <transactions/payment/PaymentOpV2Frame.h>
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

    auto requestEntry = request->getRequestEntry();
    auto invoiceRequest = requestEntry.body.invoiceRequest();

    if (!(invoiceRequest.sender == getSourceID()))
    {
        innerResult().code(ReviewRequestResultCode::ONLY_SENDER_CAN_APPROVE_INVOICE);
        return false;
    }

    Database& db = ledgerManager.getDatabase();


    auto balanceHelper = BalanceHelper::Instance();
    auto senderBalance = balanceHelper->mustLoadBalance(invoiceRequest.sender,
                                                        invoiceRequest.asset, db, &delta);
    auto receiverBalance = balanceHelper->mustLoadBalance(requestEntry.requestor,
                                                          invoiceRequest.asset, db, &delta);

    if (!checkPaymentDetails(requestEntry, receiverBalance->getBalanceID(), senderBalance->getBalanceID()))
        return false;

    if (!processPaymentV2(app, delta, ledgerManager))
        return false;

    EntryHelperProvider::storeDeleteEntry(delta, db, request->getKey());

    return true;
}

bool
ReviewInvoiceRequestOpFrame::checkPaymentDetails(ReviewableRequestEntry& requestEntry,
                                    BalanceID receiverBalance, BalanceID senderBalance)
{
    auto invoiceRequest = requestEntry.body.invoiceRequest();
    auto paymentDetails = mReviewRequest.requestDetails.billPay().paymentDetails;
    switch (paymentDetails.destination.type())
    {
        case PaymentDestinationType::BALANCE:
        {
            if (!(receiverBalance == paymentDetails.destination.balanceID()))
            {
                innerResult().code(BillPayResultCode::DESTINATION_BALANCE_MISMATCHED);
                return false;
            }
            break;
        }
        case PaymentDestinationType::ACCOUNT:
        {
            if (!(requestEntry.requestor == paymentDetails.destination.accountID()))
            {
                innerResult().code(BillPayResultCode::DESTINATION_ACCOUNT_MISMATCHED);
                return false;
            }
            break;
        }
        default:
            throw std::runtime_error("Unexpected payment v2 destination type in BillPay");
    }

    if (invoiceRequest.amount != paymentDetails.amount)
    {
        innerResult().code(BillPayResultCode::AMOUNT_MISMATCHED);
        return false;
    }

    if (!(paymentDetails.sourceBalanceID == senderBalance))
    {
        innerResult().code(BillPayResultCode::SOURCE_BALANCE_MISMATCHED);
        return false;
    }

    if (!paymentDetails.feeData.sourcePaysForDest)
    {
        innerResult().code(BillPayResultCode::REQUIRED_SOURCE_PAY_FOR_DESTINATION);
        return false;
    }

    return true;
}

bool
ReviewInvoiceRequestOpFrame::processPaymentV2(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager)
{
    Operation op;
    op.body.type(OperationType::PAYMENT_V2);
    op.body.paymentOpV2() = mReviewRequest.requestDetails.billPay().paymentDetails;

    OperationResult opRes;
    opRes.code(OperationResultCode::opINNER);
    opRes.tr().type(OperationType::PAYMENT_V2);
    PaymentOpV2Frame paymentOpV2Frame(op, opRes, mParentTx);

    paymentOpV2Frame.setSourceAccountPtr(mSourceAccount);

    if (!paymentOpV2Frame.doCheckValid(app) || !paymentOpV2Frame.doApply(app, delta, ledgerManager))
    {
        auto resultCode = PaymentOpV2Frame::getInnerCode(opRes);
        trySetErrorCode(resultCode);
        return false;
    }

    innerResult().success().paymentV2Response = opRes.tr().paymentV2Result().paymentV2Response();
    return true;
}

void
BillPayOReviewInvoiceRequestOpFramepFrame::trySetErrorCode(PaymentV2ResultCode paymentResult)
{
    try
    {
        innerResult().code(paymentCodeToBillPayCode[paymentResult]);
    }
    catch(...)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected result code from payment v2 operation: "
                                               << xdr::xdr_traits<PaymentV2ResultCode>::enum_name(paymentResult);
        throw std::runtime_error("Unexpected result code from payment v2 operation");
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

    return ReviewRequestOpFrame::handlePermanentReject(app, delta, ledgerManager, request);
}

ReviewInvoiceRequestOpFrame::ReviewInvoiceRequestOpFrame(Operation const & op, OperationResult & res,
                                                         TransactionFrame & parentTx) :
        ReviewRequestOpFrame(op, res, parentTx)
{
}

}
