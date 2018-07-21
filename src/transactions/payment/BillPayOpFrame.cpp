#include <ledger/ReviewableRequestHelper.h>
#include <xdr/Stellar-operation-bill-pay.h>
#include <ledger/BalanceHelper.h>
#include <lib/xdrpp/xdrpp/printer.h>
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
    if (!request || (request->getRequestType() != ReviewableRequestType::INVOICE))
    {
        innerResult().code(BillPayResultCode::INVOICE_REQUEST_NOT_FOUND);
        return false;
    }

    auto requestEntry = request->getRequestEntry();
    auto invoiceRequest = requestEntry.body.invoiceRequest();
    auto balanceHelper = BalanceHelper::Instance();
    auto senderBalance = balanceHelper->loadBalance(invoiceRequest.sender,
                                                    invoiceRequest.asset, db, &delta);
    auto receiverBalance = balanceHelper->loadBalance(requestEntry.requestor,
                                                      invoiceRequest.asset, db, &delta);

    if (!checkPaymentDetails(requestEntry, receiverBalance->getBalanceID(), senderBalance->getBalanceID()))
        return false;

    if (invoiceRequest.isSecured && !senderBalance->unlock(invoiceRequest.amount))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: failed to unlock specified amount in bill pay: "
                                                  "invoice request: " << xdr::xdr_to_string(requestEntry)
                                               << "; balance: " << xdr::xdr_to_string(senderBalance->getBalance());
        throw runtime_error("Unexpected state: failed to unlock specified amount in bill pay");
    }

    if (!processPaymentV2(app, delta, ledgerManager))
        return false;

    EntryHelperProvider::storeDeleteEntry(delta, db, request->getKey());

    return true;
}

bool
BillPayOpFrame::checkPaymentDetails(ReviewableRequestEntry& requestEntry,
                                    BalanceID receiverBalance, BalanceID senderBalance)
{
    auto invoiceRequest = requestEntry.body.invoiceRequest();
    switch (mBillPay.paymentDetails.destination.type())
    {
        case PaymentDestinationType::BALANCE:
        {
            if (!(receiverBalance == mBillPay.paymentDetails.destination.balanceID()))
            {
                innerResult().code(BillPayResultCode::DESTINATION_BALANCE_MISMATCHED);
                return false;
            }
            break;
        }
        case PaymentDestinationType::ACCOUNT:
        {
            if (!(requestEntry.requestor == mBillPay.paymentDetails.destination.accountID()))
            {
                innerResult().code(BillPayResultCode::DESTINATION_ACCOUNT_MISMATCHED);
                return false;
            }
            break;
        }
        default:
            throw std::runtime_error("Unexpected payment v2 destination type in BillPay");
    }

    if (invoiceRequest.amount != mBillPay.paymentDetails.amount)
    {
        innerResult().code(BillPayResultCode::AMOUNT_MISMATCHED);
        return false;
    }

    if (!(mBillPay.paymentDetails.sourceBalanceID == senderBalance))
    {
        innerResult().code(BillPayResultCode::SOURCE_BALANCE_MISMATCHED);
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
        trySetErrorCode(resultCode);
        return false;
    }

    innerResult().success().paymentV2Response = opRes.tr().paymentV2Result().paymentV2Response();
    return true;
}

void BillPayOpFrame::trySetErrorCode(PaymentV2ResultCode paymentResult)
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