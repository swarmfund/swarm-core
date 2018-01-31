// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/SetLimitsOpFrame.h"
#include "ledger/AccountTypeLimitsFrame.h"
#include "ledger/AccountTypeLimitsHelper.h"
#include "ledger/AccountLimitsFrame.h"
#include "ledger/AccountLimitsHelper.h"
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

SourceDetails SetLimitsOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                                        int32_t ledgerVersion) const
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

    auto newSignersVersion = static_cast<int32_t>(LedgerVersion::NEW_SIGNER_TYPES);
    if (ledgerVersion >= newSignersVersion)
    {
        signerType = static_cast<int32_t>(SignerType::LIMITS_MANAGER);
    }

    //disallowed
	return SourceDetails({}, threshold, signerType);
}

std::string
SetLimitsOpFrame::getInnerResultCodeAsStr()
{
    const auto result = getResult();
    const auto code = getInnerCode(result);
    return xdr::xdr_traits<SetLimitsResultCode>::enum_name(code);
}

bool
SetLimitsOpFrame::doApply(Application& app, LedgerDelta& delta,
                           LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();
    if (mSetLimits.account)
    {
		auto accountLimitsHelper = AccountLimitsHelper::Instance();
        auto accountLimitsFrame = accountLimitsHelper->loadLimits(*mSetLimits.account, db);
        if (accountLimitsFrame)
        {
            accountLimitsFrame->setLimits(mSetLimits.limits);
            EntryHelperProvider::storeChangeEntry(delta, db, accountLimitsFrame->mEntry);
        }
        else
        {
            accountLimitsFrame = AccountLimitsFrame::createNew(*mSetLimits.account,
                mSetLimits.limits);
            EntryHelperProvider::storeAddEntry(delta, db, accountLimitsFrame->mEntry);
        }
    }
    else
    {
        assert(mSetLimits.accountType);
		auto accountTypeLimitsHelper = AccountTypeLimitsHelper::Instance();
        auto accountTypeLimits = accountTypeLimitsHelper->loadLimits(*mSetLimits.accountType, db, &delta);
        if (accountTypeLimits)
        {
            accountTypeLimits->setLimits(mSetLimits.limits);
            EntryHelperProvider::storeChangeEntry(delta, db, accountTypeLimits->mEntry);
        }
        else
        {
            LedgerEntry le;
            le.data.type(LedgerEntryType::ACCOUNT_TYPE_LIMITS);
            AccountTypeLimitsEntry& entry = le.data.accountTypeLimits();

            entry.accountType = *mSetLimits.accountType;
            entry.limits = mSetLimits.limits;
            auto accountTypeLimitsFrame = std::make_shared<AccountTypeLimitsFrame>(le);
			EntryHelperProvider::storeAddEntry(delta, db, accountTypeLimitsFrame->mEntry);
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
