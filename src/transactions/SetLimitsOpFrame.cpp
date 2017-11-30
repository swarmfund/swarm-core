// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/SetLimitsOpFrame.h"
#include "ledger/AccountTypeLimitsFrame.h"
#include "ledger/AccountLimitsFrame.h"
#include "database/Database.h"
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"

namespace stellar
{
using xdr::operator==;

SetLimitsOpFrame::SetLimitsOpFrame(Operation const& op, OperationResult& res,
                                     TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mSetLimits(mOperation.body.setLimitsOp())
{
}


std::unordered_map<AccountID, CounterpartyDetails> SetLimitsOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
{
	if (!mSetLimits.account)
		return{};
	return{
		{ *mSetLimits.account , CounterpartyDetails(getAllAccountTypes(), true, true) }
	};
}

SourceDetails SetLimitsOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const
{
	int32_t signerType = static_cast<int32_t>(SignerType::LIMITS_MANAGER);
	int32_t threshold = mSourceAccount->getHighThreshold();
	if (mSetLimits.account)
	{
		auto account = counterpartiesDetails.find(*mSetLimits.account);
		if (account == counterpartiesDetails.end() || !account->second.mAccount)
			throw std::invalid_argument("Unexpected counterpartiesDetails. Expected counterparty to be included");

		if (account->second.mAccount->getAccountType() == AccountType::GENERAL)
		{
			signerType |= static_cast<int32_t>(SignerType::GENERAL_ACC_MANAGER);
			threshold = mSourceAccount->getLowThreshold();
		}
	}
	return SourceDetails({ AccountType::MASTER }, threshold, signerType);
}

bool
SetLimitsOpFrame::doApply(Application& app, LedgerDelta& delta,
                           LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();
    if (mSetLimits.account)
    {
        auto accountLimitsFrame = AccountLimitsFrame::loadLimits(*mSetLimits.account, db);
        if (accountLimitsFrame)
        {
            accountLimitsFrame->setLimits(mSetLimits.limits);
            accountLimitsFrame->storeChange(delta, db);
        }
        else
        {
            accountLimitsFrame = AccountLimitsFrame::createNew(*mSetLimits.account,
                mSetLimits.limits);
            accountLimitsFrame->storeAdd(delta, db);
        }
    }
    else
    {
        assert(mSetLimits.accountType);
        auto accountTypeLimits = AccountTypeLimitsFrame::loadLimits(*mSetLimits.accountType, db, &delta);
        if (accountTypeLimits)
        {
            accountTypeLimits->setLimits(mSetLimits.limits);
            accountTypeLimits->storeChange(delta, db);
        }
        else
        {
            LedgerEntry le;
            le.data.type(LedgerEntryType::ACCOUNT_TYPE_LIMITS);
            AccountTypeLimitsEntry& entry = le.data.accountTypeLimits();

            entry.accountType = *mSetLimits.accountType;
            entry.limits = mSetLimits.limits;
            auto accountTypeLimitsFrame = std::make_shared<AccountTypeLimitsFrame>(le);
            accountTypeLimitsFrame->storeAdd(delta, db);
        }
    }

    app.getMetrics().NewMeter({"op-set-limits", "success", "apply"}, "operation")
        .Mark();
    innerResult().code(SetLimitsResultCode::SUCCESS);
    return true;
}

bool
SetLimitsOpFrame::doCheckValid(Application& app)
{
    if (mSetLimits.account && mSetLimits.accountType)
    {
        app.getMetrics().NewMeter(
                    {"op-set-limits", "invalid", "malformed"},
                    "operation").Mark();
        innerResult().code(SetLimitsResultCode::MALFORMED);
        return false;
    }

    if (!mSetLimits.account && !mSetLimits.accountType)
    {
        app.getMetrics().NewMeter(
                    {"op-set-limits", "invalid", "malformed"},
                    "operation").Mark();
        innerResult().code(SetLimitsResultCode::MALFORMED);
        return false;
    }
    
    if (!isValidLimits())
    {
        app.getMetrics().NewMeter(
                    {"op-set-limits", "invalid", "malformed"},
                    "operation").Mark();
        innerResult().code(SetLimitsResultCode::MALFORMED);
        return false;
    }

    return true;
}

bool SetLimitsOpFrame::isValidLimits()
{
    auto& limits = mSetLimits.limits;
	return AccountFrame::isLimitsValid(limits);
}

}
