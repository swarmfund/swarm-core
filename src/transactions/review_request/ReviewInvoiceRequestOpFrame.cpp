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
#include "ledger/ContractHelper.h"
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

    if (!invoiceRequest.contractID)
        return true;

    auto contractHelper = ContractHelper::Instance();
    auto contractFrame = contractHelper->loadContract(*invoiceRequest.contractID, db, &delta);

    if (!contractFrame)
    {
        innerResult().code(ReviewRequestResultCode::CONTRACT_NOT_FOUND);
        return false;
    }

    return tryLockAmount(receiverBalance, invoiceRequest.amount);
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
                innerResult().code(ReviewRequestResultCode::INVOICE_RECIEVER_BALANCE_LOCK_AMOUNT_OVERFLOW);
                return false;
            }
            case BalanceFrame::UNDERFUNDED:
            {
                CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state. There must be enough amount "
                                                       << static_cast<int>(lockResult);
                throw std::runtime_error("Unexpected state. There must be enough amount");
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
                innerResult().code(ReviewRequestResultCode::DESTINATION_BALANCE_MISMATCHED);
                return false;
            }
            break;
        }
        case PaymentDestinationType::ACCOUNT:
        {
            if (!(requestEntry.requestor == paymentDetails.destination.accountID()))
            {
                innerResult().code(ReviewRequestResultCode::DESTINATION_ACCOUNT_MISMATCHED);
                return false;
            }
            break;
        }
        default:
            throw std::runtime_error("Unexpected payment v2 destination type in BillPay");
    }

    if (invoiceRequest.amount != paymentDetails.amount)
    {
        innerResult().code(ReviewRequestResultCode::AMOUNT_MISMATCHED);
        return false;
    }

    if (!(paymentDetails.sourceBalanceID == senderBalance))
    {
        innerResult().code(ReviewRequestResultCode::SOURCE_BALANCE_MISMATCHED);
        return false;
    }

    if (!paymentDetails.feeData.sourcePaysForDest)
    {
        innerResult().code(ReviewRequestResultCode::REQUIRED_SOURCE_PAY_FOR_DESTINATION);
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

    if (ledgerManager.shouldUse(LedgerVersion::ADD_REVIEW_INVOICE_REQUEST_PAYMENT_RESPONSE))
    {
        innerResult().success().ext.v(LedgerVersion::ADD_REVIEW_INVOICE_REQUEST_PAYMENT_RESPONSE);
        innerResult().success().ext.paymentV2Response() = opRes.tr().paymentV2Result().paymentV2Response();
    }

    return true;
}

void
ReviewInvoiceRequestOpFrame::trySetErrorCode(PaymentV2ResultCode paymentResult)
{
    try
    {
        innerResult().code(paymentCodeToReviewRequestCode[paymentResult]);
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
