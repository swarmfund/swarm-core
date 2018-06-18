// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <ledger/LimitsV2Helper.h>
#include "transactions/ManageLimitsOpFrame.h"
#include "ledger/AccountTypeLimitsFrame.h"
#include "ledger/AccountTypeLimitsHelper.h"
#include "ledger/AccountLimitsFrame.h"
#include "ledger/AccountLimitsHelper.h"
#include "database/Database.h"
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"
#include "ledger/LedgerDelta.h"

namespace stellar
{
using xdr::operator==;

ManageLimitsOpFrame::ManageLimitsOpFrame(Operation const& op, OperationResult& res,
                                     TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mManageLimits(mOperation.body.manageLimitsOp())
{
}


std::unordered_map<AccountID, CounterpartyDetails> ManageLimitsOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
{
	if (!mManageLimits.accountID)
		return{};
	return{
		{ *mManageLimits.accountID , CounterpartyDetails(getAllAccountTypes(), true, true) }
	};
}

SourceDetails ManageLimitsOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                                        int32_t ledgerVersion) const
{
	int32_t signerType = static_cast<int32_t>(SignerType::LIMITS_MANAGER);
	int32_t threshold = mSourceAccount->getHighThreshold();
	if (mManageLimits.accountID)
	{
		auto account = counterpartiesDetails.find(*mManageLimits.accountID);
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

	return SourceDetails({AccountType::GENERAL, AccountType::MASTER}, threshold, signerType);
}

std::string
ManageLimitsOpFrame::getInnerResultCodeAsStr()
{
    const auto result = getResult();
    const auto code = getInnerCode(result);
    return xdr::xdr_traits<ManageLimitsResultCode>::enum_name(code);
}

/*bool
ManageLimitsOpFrame::doApply(Application& app, LedgerDelta& delta,
                           LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();
    if (mManageLimits.accountID)
    {
		auto accountLimitsHelper = AccountLimitsHelper::Instance();
        auto accountLimitsFrame = accountLimitsHelper->loadLimits(*mManageLimits.account, db);
        if (accountLimitsFrame)
        {
            accountLimitsFrame->manageLimits(mManageLimits.limits);
            EntryHelperProvider::storeChangeEntry(delta, db, accountLimitsFrame->mEntry);
        }
        else
        {
            accountLimitsFrame = AccountLimitsFrame::createNew(*mManageLimits.account,
                mManageLimits.limits);
            EntryHelperProvider::storeAddEntry(delta, db, accountLimitsFrame->mEntry);
        }
    }
    else
    {
        assert(mManageLimits.accountType);
		auto accountTypeLimitsHelper = AccountTypeLimitsHelper::Instance();
        auto accountTypeLimits = accountTypeLimitsHelper->loadLimits(*mManageLimits.accountType, db, &delta);
        if (accountTypeLimits)
        {
            accountTypeLimits->manageLimits(mManageLimits.limits);
            EntryHelperProvider::storeChangeEntry(delta, db, accountTypeLimits->mEntry);
        }
        else
        {
            LedgerEntry le;
            le.data.type(LedgerEntryType::ACCOUNT_TYPE_LIMITS);
            AccountTypeLimitsEntry& entry = le.data.accountTypeLimits();

            entry.accountType = *mManageLimits.accountType;
            entry.limits = mManageLimits.limits;
            auto accountTypeLimitsFrame = std::make_shared<AccountTypeLimitsFrame>(le);
			EntryHelperProvider::storeAddEntry(delta, db, accountTypeLimitsFrame->mEntry);
        }
    }

    app.getMetrics().NewMeter({"op-set-limits", "success", "apply"}, "operation")
        .Mark();
    innerResult().code(ManageLimitsResultCode::SUCCESS);
    return true;
}*/

bool
ManageLimitsOpFrame::doApply(Application& app, LedgerDelta& delta,
                             LedgerManager& ledgerManager)
{
    innerResult().code(ManageLimitsResultCode::SUCCESS);

    Database& db = ledgerManager.getDatabase();
    auto limitsV2Helper = LimitsV2Helper::Instance();
    auto limitsV2Frame = limitsV2Helper->loadLimits(db, mManageLimits.statsOpType, mManageLimits.assetCode,
                                                    mManageLimits.accountID, mManageLimits.accountType,
                                                    mManageLimits.isConvertNeeded, &delta);
    if (mManageLimits.isDelete)
    {
        if (!limitsV2Frame)
        {
            innerResult().code(ManageLimitsResultCode::NOT_FOUND);
            return false;
        }

        limitsV2Helper->storeDelete(delta, db, limitsV2Frame->getKey());
        return true;
    }

    if (!limitsV2Frame)
    {
        uint64_t id = delta.getHeaderFrame().generateID(LedgerEntryType::LIMITS_V2);
        limitsV2Frame = LimitsV2Frame::createNew(id, mManageLimits);
        limitsV2Helper->storeAdd(delta, db, limitsV2Frame->mEntry);
        return true;
    }

    limitsV2Frame->changeLimits(mManageLimits);
    limitsV2Helper->storeChange(delta, db, limitsV2Frame->mEntry);

    app.getMetrics().NewMeter({"op-manage-limits", "success", "apply"}, "operation").Mark();

    return true;
}

bool
ManageLimitsOpFrame::doCheckValid(Application& app)
{
    if (!!mManageLimits.accountID && !!mManageLimits.accountType)
    {
        app.getMetrics().NewMeter(
                    {"op-set-limits", "invalid", "malformed"},
                    "operation").Mark();
        innerResult().code(ManageLimitsResultCode::MALFORMED);
        return false;
    }
    
    if (!isValidLimits())
    {
        app.getMetrics().NewMeter(
                    {"op-set-limits", "invalid", "malformed"},
                    "operation").Mark();
        innerResult().code(ManageLimitsResultCode::MALFORMED);
        return false;
    }

    return true;
}

bool ManageLimitsOpFrame::isValidLimits()
{
    if (mManageLimits.dailyOut < 0)
        return false;
    if (mManageLimits.weeklyOut < mManageLimits.dailyOut)
        return false;
    if (mManageLimits.monthlyOut < mManageLimits.weeklyOut)
        return false;
    return mManageLimits.annualOut >= mManageLimits.monthlyOut;
}

}
