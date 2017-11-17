// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/asio.h"
#include "transactions/DirectDebitOpFrame.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "ledger/TrustFrame.h"
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"
#include "transactions/PaymentOpFrame.h"
#include "util/Logging.h"
#include <algorithm>

namespace stellar
{

using namespace std;
using xdr::operator==;

std::unordered_map<AccountID, CounterpartyDetails> DirectDebitOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
{
	return{
		{mDirectDebit.from, CounterpartyDetails({GENERAL, OPERATIONAL, COMMISSION }, false, true)}
	};
}

SourceDetails DirectDebitOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const
{
	return SourceDetails({ NOT_VERIFIED, GENERAL, OPERATIONAL, COMMISSION }, mSourceAccount->getMediumThreshold(), SIGNER_DIRECT_DEBIT_OPERATOR);
}

DirectDebitOpFrame::DirectDebitOpFrame(Operation const& op, OperationResult& res,
                               TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx), mDirectDebit(mOperation.body.directDebitOp())
{
}

bool
DirectDebitOpFrame::doApply(Application& app, LedgerDelta& delta,
                        LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();

    if (!TrustFrame::exists(db, getSourceID(), mDirectDebit.paymentOp.sourceBalanceID))
    {
        app.getMetrics()
            .NewMeter({"op-direct-debit", "failed", "apply"}, "operation")
            .Mark();
        innerResult().code(DIRECT_DEBIT_NO_TRUST);
        return false;
    }

    Operation op;
    op.sourceAccount.activate() = mDirectDebit.from;
    op.body.type(PAYMENT);
    op.body.paymentOp() = mDirectDebit.paymentOp;

    OperationResult opRes;
    opRes.code(opINNER);
    opRes.tr().type(PAYMENT);
    PaymentOpFrame payment(op, opRes, mParentTx);
    
    auto fromAccount = AccountFrame::loadAccount(delta, mDirectDebit.from, db);
    
    payment.setSourceAccountPtr(fromAccount);

    if (!payment.doCheckValid(app) ||
        !payment.doApply(app, delta, ledgerManager))
    {
        if (payment.getResultCode() != opINNER)
        {
            throw std::runtime_error("Unexpected error code from payment");
        }
        auto paymentCode = PaymentOpFrame::getInnerCode(payment.getResult());
        DirectDebitResultCode res = paymentCodeToDebitCode[paymentCode];
        innerResult().code(res);
        return false;
    }

    assert(PaymentOpFrame::getInnerCode(payment.getResult()) ==
           PAYMENT_SUCCESS);
    auto paymentResult = payment.getResult();
    innerResult().success().paymentResponse = paymentResult.tr().paymentResult().paymentResponse();

    app.getMetrics()
        .NewMeter({"op-direct-debit", "success", "apply"}, "operation")
        .Mark();
    innerResult().code(DIRECT_DEBIT_SUCCESS);

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
        innerResult().code(DIRECT_DEBIT_MALFORMED);
        return false;
    }
    return true;
}
}
