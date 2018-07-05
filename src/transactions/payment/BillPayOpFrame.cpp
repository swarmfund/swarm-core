#include <ledger/ReviewableRequestHelper.h>
#include <xdr/Stellar-operation-bill-pay.h>
#include "BillPayOpFrame.h"
#include "PaymentOpV2Frame.h"

namespace stellar
{
using namespace std;
using xdr::operator==;

std::unordered_map<AccountID, CounterpartyDetails>
BillPayOpFrame::getCounterpartyDetails(Database &db, LedgerDelta *delta) const {
    // there are no restrictions for counterparty to receive payment on current stage,
    // so no need to load it.
    return {};
}

SourceDetails
BillPayOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                        int32_t ledgerVersion) const {
    auto signerType = static_cast<int32_t>(SignerType::BALANCE_MANAGER);
    switch (mSourceAccount->getAccountType()) {
        case AccountType::OPERATIONAL:
            signerType = static_cast<int32_t>(SignerType::OPERATIONAL_BALANCE_MANAGER);
            break;
        case AccountType::COMMISSION:
            signerType = static_cast<int32_t>(SignerType::COMMISSION_BALANCE_MANAGER);
            break;
        default:
            break;
    }

    std::vector<AccountType> allowedAccountTypes = {AccountType::NOT_VERIFIED, AccountType::GENERAL,
                                                    AccountType::OPERATIONAL, AccountType::COMMISSION,
                                                    AccountType::SYNDICATE, AccountType::EXCHANGE,
                                                    AccountType::ACCREDITED_INVESTOR,
                                                    AccountType::INSTITUTIONAL_INVESTOR,
                                                    AccountType::VERIFIED};

    return SourceDetails(allowedAccountTypes, mSourceAccount->getMediumThreshold(), signerType,
                         static_cast<int32_t>(BlockReasons::TOO_MANY_KYC_UPDATE_REQUESTS));
}

BillPayOpFrame::BillPayOpFrame(const stellar::Operation &op, stellar::OperationResult &res,
                               stellar::TransactionFrame &parentTx)
        : OperationFrame(op, res, parentTx), mBillPay(mOperation.body.billPayOp())
{
}

bool
BillPayOpFrame::doApply(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager)
{
    innerResult().code(BillPayResultCode::SUCCESS);
    Database& db = ledgerManager.getDatabase();

    auto request = ReviewableRequestHelper::Instance()->loadRequest(mBillPay.requestID, db, &delta);
    if (!request || (request->getRequestType() != ReviewableRequestType::MANAGE_INVOICE))
    {
        innerResult().code(BillPayResultCode::INVOICE_REQUEST_NOT_FOUND);
        return false;
    }

    auto invoiceEntry = request->getRequestEntry().body.invoiceRequestEntry();
    if (!checkPaymentDetails(invoiceEntry))
        return false;

    if (!processPaymentV2(app, delta, ledgerManager))
        return false;

    EntryHelperProvider::storeDeleteEntry(delta, db, request->getKey());

    return true;
}

bool
BillPayOpFrame::checkPaymentDetails(InvoiceRequestEntry& invoiceRequestEntry)
{
    switch (mBillPay.paymentDetails.destination.type())
    {
        case PaymentDestinationType::BALANCE:
        {
            if (!(invoiceRequestEntry.invoiceRequest.receiverBalance ==
                  mBillPay.paymentDetails.destination.balanceID()))
            {
                innerResult().code(BillPayResultCode::DESTINATION_BALANCE_MISMATCHED);
                return false;
            }
            break;
        }
        case PaymentDestinationType::ACCOUNT:
        {
            if (!(invoiceRequestEntry.receiverAccount == mBillPay.paymentDetails.destination.accountID()))
            {
                innerResult().code(BillPayResultCode::DESTINATION_ACCOUNT_MISMATCHED);
                return false;
            }
            break;
        }
        default:
            throw std::runtime_error("Unexpected payment v2 destination type in BillPay");
    }

    if (invoiceRequestEntry.invoiceRequest.amount != mBillPay.paymentDetails.amount)
    {
        innerResult().code(BillPayResultCode::AMOUNT_MISMATCHED);
        return false;
    }

    if (!mBillPay.paymentDetails.feeData.sourcePaysForDest)
    {
        innerResult().code(BillPayResultCode::REQUIRED_SOURCE_PAY_FOR_DESTINATION);
        return false;
    }

    return true;
}

bool BillPayOpFrame::processPaymentV2(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager)
{
    Operation op;
    op.body.type(OperationType::PAYMENT_V2);
    op.body.paymentOpV2() = mBillPay.paymentDetails;

    OperationResult opRes;
    opRes.code(OperationResultCode::opINNER);
    opRes.tr().type(OperationType::PAYMENT_V2);
    PaymentOpV2Frame paymentOpV2Frame(op, opRes, mParentTx);

    paymentOpV2Frame.setSourceAccountPtr(mSourceAccount);

    if (!paymentOpV2Frame.doCheckValid(app) || !paymentOpV2Frame.doApply(app, delta, ledgerManager))
    {
        auto resultCode = PaymentOpV2Frame::getInnerCode(opRes);
        setErrorCode(resultCode);
        return false;
    }

    innerResult().success().paymentV2Response = opRes.tr().paymentV2Result().paymentV2Response();
    return true;
}

void BillPayOpFrame::setErrorCode(PaymentV2ResultCode paymentResult)
{
    switch (paymentResult)
    {
        case PaymentV2ResultCode::MALFORMED:
        {
            innerResult().code(BillPayResultCode::MALFORMED);
            return;
        }
        case PaymentV2ResultCode::UNDERFUNDED:
        {
            innerResult().code(BillPayResultCode::UNDERFUNDED);
            return;
        }
        case PaymentV2ResultCode::LINE_FULL:
        {
            innerResult().code(BillPayResultCode::LINE_FULL);
            return;
        }
        case PaymentV2ResultCode::DESTINATION_BALANCE_NOT_FOUND:
        {
            innerResult().code(BillPayResultCode::DESTINATION_BALANCE_NOT_FOUND);
            return;
        }
        case PaymentV2ResultCode::BALANCE_ASSETS_MISMATCHED:
        {
            innerResult().code(BillPayResultCode::BALANCE_ASSETS_MISMATCHED);
            return;
        }
        case PaymentV2ResultCode::SRC_BALANCE_NOT_FOUND:
        {
            innerResult().code(BillPayResultCode::SRC_BALANCE_NOT_FOUND);
            return;
        }
        case PaymentV2ResultCode::REFERENCE_DUPLICATION:
        {
            innerResult().code(BillPayResultCode::REFERENCE_DUPLICATION);
            return;
        }
        case PaymentV2ResultCode::STATS_OVERFLOW:
        {
            innerResult().code(BillPayResultCode::STATS_OVERFLOW);
            return;
        }
        case PaymentV2ResultCode::LIMITS_EXCEEDED:
        {
            innerResult().code(BillPayResultCode::LIMITS_EXCEEDED);
            return;
        }
        case PaymentV2ResultCode::NOT_ALLOWED_BY_ASSET_POLICY:
        {
            innerResult().code(BillPayResultCode::NOT_ALLOWED_BY_ASSET_POLICY);
            return;
        }
        case PaymentV2ResultCode::INVALID_DESTINATION_FEE:
        {
            innerResult().code(BillPayResultCode::INVALID_DESTINATION_FEE);
            return;
        }
        case PaymentV2ResultCode::INVALID_DESTINATION_FEE_ASSET:
        {
            innerResult().code(BillPayResultCode::INVALID_DESTINATION_FEE_ASSET);
            return;
        }
        case PaymentV2ResultCode::FEE_ASSET_MISMATCHED:
        {
            innerResult().code(BillPayResultCode::FEE_ASSET_MISMATCHED);
            return;
        }
        case PaymentV2ResultCode::INSUFFICIENT_FEE_AMOUNT:
        {
            innerResult().code(BillPayResultCode::INSUFFICIENT_FEE_AMOUNT);
            return;
        }
        case PaymentV2ResultCode::BALANCE_TO_CHARGE_FEE_FROM_NOT_FOUND:
        {
            innerResult().code(BillPayResultCode::BALANCE_TO_CHARGE_FEE_FROM_NOT_FOUND);
            return;
        }
        case PaymentV2ResultCode::PAYMENT_AMOUNT_IS_LESS_THAN_DEST_FEE:
        {
            innerResult().code(BillPayResultCode::PAYMENT_AMOUNT_IS_LESS_THAN_DEST_FEE);
            return;
        }
        default:
        {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected result code from payment v2 operation: "
                                                   << xdr::xdr_traits<PaymentV2ResultCode>::enum_name(paymentResult);
            throw std::runtime_error("Unexpected result code from payment v2 operation");
        }
    }
}

bool
BillPayOpFrame::doCheckValid(Application &app)
{
    if (mBillPay.requestID == 0)
    {
        innerResult().code(BillPayResultCode::MALFORMED);
        return false;
    }

    return true;
}

}