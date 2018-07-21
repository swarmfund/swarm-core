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