// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "database/Database.h"
#include "ledger/AccountHelper.h"
#include "ledger/LedgerDelta.h"
#include "ledger/StorageHelper.h"
#include "ledger/TrustFrame.h"
#include "ledger/TrustHelper.h"
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"
#include "transactions/DirectDebitOpFrame.h"
#include "transactions/payment/PaymentOpFrame.h"
#include "util/Logging.h"
#include "util/asio.h"
#include <algorithm>

namespace stellar
{

using namespace std;
using xdr::operator==;

std::unordered_map<AccountID, CounterpartyDetails> DirectDebitOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
{
	return{
		{mDirectDebit.from, CounterpartyDetails({AccountType::GENERAL, AccountType::OPERATIONAL, AccountType::COMMISSION,
                                                 AccountType::EXCHANGE, AccountType::VERIFIED,
                                                 AccountType::ACCREDITED_INVESTOR, AccountType::INSTITUTIONAL_INVESTOR},
                                                 false, true)}
	};
}

SourceDetails DirectDebitOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                                          int32_t ledgerVersion) const
{
    vector<AccountType> allowedAccountTypes = { AccountType::NOT_VERIFIED, AccountType::GENERAL, AccountType::OPERATIONAL,
                                                AccountType::COMMISSION, AccountType::EXCHANGE, AccountType::VERIFIED,
                                                AccountType::ACCREDITED_INVESTOR, AccountType::INSTITUTIONAL_INVESTOR};
    // disallowed
	return SourceDetails({}, mSourceAccount->getMediumThreshold(),
                         static_cast<int32_t >(SignerType::DIRECT_DEBIT_OPERATOR),
                         static_cast<int32_t>(BlockReasons::TOO_MANY_KYC_UPDATE_REQUESTS) |
                         static_cast<uint32_t>(BlockReasons::WITHDRAWAL));
}

DirectDebitOpFrame::DirectDebitOpFrame(Operation const& op, OperationResult& res,
                               TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx), mDirectDebit(mOperation.body.directDebitOp())
{
}

bool
DirectDebitOpFrame::doApply(Application& app, StorageHelper& storageHelper,
                        LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();

	auto trustHelper = TrustHelper::Instance();
    if (!trustHelper->exists(db, getSourceID(), mDirectDebit.paymentOp.sourceBalanceID))
    {
        app.getMetrics()
            .NewMeter({"op-direct-debit", "failed", "apply"}, "operation")
            .Mark();
        innerResult().code(DirectDebitResultCode::NO_TRUST);
        return false;
    }

    Operation op;
    op.sourceAccount.activate() = mDirectDebit.from;
    op.body.type(OperationType::PAYMENT);
    op.body.paymentOp() = mDirectDebit.paymentOp;

    OperationResult opRes;
    opRes.code(OperationResultCode::opINNER);
    opRes.tr().type(OperationType::PAYMENT);
    PaymentOpFrame payment(op, opRes, mParentTx);
    
	auto accountHelper = AccountHelper::Instance();
    auto fromAccount = accountHelper->loadAccount(
        mDirectDebit.from, db, storageHelper.getLedgerDelta());
    
    payment.setSourceAccountPtr(fromAccount);

    if (!payment.doCheckValid(app) ||
        !payment.doApply(app, storageHelper, ledgerManager))
    {
        if (payment.getResultCode() != OperationResultCode::opINNER)
        {
            throw std::runtime_error("Unexpected error code from payment");
        }
        auto paymentCode = PaymentOpFrame::getInnerCode(payment.getResult());
        DirectDebitResultCode res = paymentCodeToDebitCode[paymentCode];
        innerResult().code(res);
        return false;
    }

    assert(PaymentOpFrame::getInnerCode(payment.getResult()) ==
           PaymentResultCode::SUCCESS);
    auto paymentResult = payment.getResult();
    innerResult().success().paymentResponse = paymentResult.tr().paymentResult().paymentResponse();

    app.getMetrics()
        .NewMeter({"op-direct-debit", "success", "apply"}, "operation")
        .Mark();
    innerResult().code(DirectDebitResultCode::SUCCESS);

    return true;
}

bool
DirectDebitOpFrame::doCheckValid(Application& app)
{
    if (mDirectDebit.from == getSourceID())
    {
        app.getMetrics()
            .NewMeter({"op-direct-debit", "invalid", "malformed-from-self"},
                      "operation")
            .Mark();
        innerResult().code(DirectDebitResultCode::MALFORMED);
        return false;
    }
    return true;
}
}
