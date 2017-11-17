// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/ManageForfeitRequestOpFrame.h"
#include "database/Database.h"
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"
#include "ledger/LedgerDelta.h"

namespace stellar
{
using xdr::operator==;


std::unordered_map<AccountID, CounterpartyDetails> ManageForfeitRequestOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
{
	// source account is only counterparty
	return {};
}

SourceDetails ManageForfeitRequestOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const
{
	return SourceDetails({ GENERAL, OPERATIONAL }, mSourceAccount->getMediumThreshold(), SIGNER_BALANCE_MANAGER);
}

ManageForfeitRequestOpFrame::ManageForfeitRequestOpFrame(Operation const& op, OperationResult& res,
                                     TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mManageForfeitRequest(mOperation.body.manageForfeitRequestOp())
{
}


bool
ManageForfeitRequestOpFrame::doApply(Application& app, LedgerDelta& delta,
                           LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();
	innerResult().code(MANAGE_FORFEIT_REQUEST_SUCCESS);

    auto balance = BalanceFrame::loadBalance(mManageForfeitRequest.balance, db);

    if (!balance || !(balance->getAccountID() == mSourceAccount->getID()) )
    {
        app.getMetrics().NewMeter({ "op-manage-forfeit-request", "invalid", "balance-not-found" },
                "operation").Mark();
        innerResult().code(MANAGE_FORFEIT_REQUEST_BALANCE_MISMATCH);
        return false;
    }

	auto requestReviewerID = mManageForfeitRequest.reviewer;
    auto reviewer = AccountFrame::loadAccount(requestReviewerID, db, &delta);
    if (!reviewer)
    {
        app.getMetrics().NewMeter({ "op-manage-forfeit-request", "invalid", "request-reviewer-not-found" },
                                  "operation").Mark();
        innerResult().code(MANAGE_FORFEIT_REQUEST_REVIEWER_NOT_FOUND);
        return false;
    }

    uint64 paymentID = delta.getHeaderFrame().generateID();

    int64 amountToCharge = mManageForfeitRequest.amount;
    AccountManager accountManager(app, db, delta, ledgerManager);

    int64_t feeToPay = calculateFee(db, balance->getAsset(), amountToCharge);
    amountToCharge += feeToPay;
    if (feeToPay != mManageForfeitRequest.totalFee)
    {
        app.getMetrics().NewMeter({"op-manage-forfeit-request", "invalid", "fee-mismatch" },
            "operation").Mark();
        innerResult().code(MANAGE_FORFEIT_REQUEST_FEE_MISMATCH);
        return false;
    }

	int64 universalAmount;
	auto account = AccountFrame::loadAccount(delta, balance->getAccountID(), db);
    auto transferResult = accountManager.processTransfer(account, balance,
        amountToCharge, universalAmount, true);

    if (!processBalanceChange(app, transferResult))
        return false;

    createPaymentRequest(paymentID, balance->getBalanceID(), amountToCharge,
            universalAmount,
            nullptr, mManageForfeitRequest.amount, delta, db, ledgerManager.getCloseTime());

    balance->storeChange(delta, db);

    innerResult().success().paymentID = paymentID;
    app.getMetrics().NewMeter({ "op-manage-forfeit-request", "success", "apply" },
            "operation").Mark();
	return true;
}

int64_t
ManageForfeitRequestOpFrame::calculateFee(Database &db, AssetCode asset, int64_t amount)
{
    bool isFeeNotRequired = isSystemAccountType(getSourceAccount().getAccountType());
    if (isFeeNotRequired)
    {
        return 0;
    }

    auto feeFrame = FeeFrame::loadForAccount(FORFEIT_FEE, asset, FeeFrame::SUBTYPE_ANY, mSourceAccount, amount, db);
    if (!feeFrame)
    {
        return 0;
    }

    return feeFrame->getFee().fixedFee + feeFrame->calculatePercentFee(amount);
}

bool
ManageForfeitRequestOpFrame::processBalanceChange(Application& app, AccountManager::Result balanceChangeResult)
{
    if (balanceChangeResult == AccountManager::Result::UNDERFUNDED)
    {
        app.getMetrics().NewMeter({ "op-manage-forfeit-request", "failure", "underfunded" }, "operation").Mark();
        innerResult().code(MANAGE_FORFEIT_REQUEST_UNDERFUNDED);
        return false;
    }

    if (balanceChangeResult == AccountManager::Result::STATS_OVERFLOW)
    {
        app.getMetrics().NewMeter({ "op-manage-forfeit-request", "failure", "stats-overflow"}, "operation").Mark();
        innerResult().code(MANAGE_FORFEIT_REQUEST_STATS_OVERFLOW);
        return false;
    }

    if (balanceChangeResult == AccountManager::Result::LIMITS_EXCEEDED)
    {
        app.getMetrics().NewMeter({ "op-manage-forfeit-request", "failure", "limits-exceeded"}, "operation").Mark();
        innerResult().code(MANAGE_FORFEIT_REQUEST_LIMITS_EXCEEDED);
        return false;
    }
    return true;
}

bool
ManageForfeitRequestOpFrame::doCheckValid(Application& app)
{
    if (mManageForfeitRequest.amount <= 0)
    {
        app.getMetrics().NewMeter({"op-manage-forfeit-request", "invalid", "malformed-negative-amount"},
                         "operation").Mark();
        innerResult().code(MANAGE_FORFEIT_REQUEST_INVALID_AMOUNT);
        return false;
    }

    if (mManageForfeitRequest.details.size() > app.getWithdrawalDetailsMaxLength())
    {
        app.getMetrics().NewMeter({"op-manage-forfeit-request", "invalid", "malformed-details-length"}, "operation").Mark();
        innerResult().code(MANAGE_FORFEIT_REQUEST_INVALID_DETAILS);
        return false;
    }

    return true;
}

}
