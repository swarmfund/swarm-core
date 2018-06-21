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


std::unordered_map<AccountID, CounterpartyDetails>
ManageLimitsOpFrame::getCounterpartyDetails(Database & db, LedgerDelta* delta) const
{
    switch (mManageLimits.details.action())
    {
        case ManageLimitsAction::CREATE:
            if (!mManageLimits.details.limitsCreateDetails().accountID)
                return{};
            return{
                    {*mManageLimits.details.limitsCreateDetails().accountID,
                            CounterpartyDetails(getAllAccountTypes(), true, true)}
            };
        case ManageLimitsAction::DELETE:
            return {
                {mSourceAccount->getID(), CounterpartyDetails({AccountType::MASTER}, true, true)}
            };
        default:
            throw std::runtime_error("Unexpected manage limits action while get counterparty details. "
                                     "Expected UPDATE or DELETE");
    }

}

SourceDetails
ManageLimitsOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                             int32_t ledgerVersion) const
{
	auto signerType = static_cast<int32_t>(SignerType::LIMITS_MANAGER);
	int32_t threshold = mSourceAccount->getHighThreshold();
	if ((mManageLimits.details.action() == ManageLimitsAction::CREATE) &&
        (!!mManageLimits.details.limitsCreateDetails().accountID))
	{
		auto account = counterpartiesDetails.find(*mManageLimits.details.limitsCreateDetails().accountID);
		if (account == counterpartiesDetails.end() || !account->second.mAccount)
			throw std::invalid_argument("Unexpected counterpartiesDetails. Expected counterparty to be included");

		if (account->second.mAccount->getAccountType() == AccountType::GENERAL)
		{
			signerType |= static_cast<int32_t>(SignerType::GENERAL_ACC_MANAGER);
			threshold = mSourceAccount->getLowThreshold();
		}
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

bool
ManageLimitsOpFrame::doApply(Application& app, LedgerDelta& delta,
                             LedgerManager& ledgerManager)
{
    innerResult().code(ManageLimitsResultCode::SUCCESS);

    Database& db = ledgerManager.getDatabase();
    auto limitsV2Helper = LimitsV2Helper::Instance();

    switch (mManageLimits.details.action())
    {
    case ManageLimitsAction::CREATE:
    {
        auto limitsV2Frame = limitsV2Helper->loadLimits(db, mManageLimits.details.limitsCreateDetails().statsOpType,
                                                        mManageLimits.details.limitsCreateDetails().assetCode,
                                                        mManageLimits.details.limitsCreateDetails().accountID,
                                                        mManageLimits.details.limitsCreateDetails().accountType,
                                                        mManageLimits.details.limitsCreateDetails().isConvertNeeded,
                                                        &delta);
        if (!limitsV2Frame) {
            uint64_t id = delta.getHeaderFrame().generateID(LedgerEntryType::LIMITS_V2);
            limitsV2Frame = LimitsV2Frame::createNew(id, mManageLimits);
            limitsV2Helper->storeAdd(delta, db, limitsV2Frame->mEntry);
            return true;
        }

        limitsV2Frame->changeLimits(mManageLimits);
        limitsV2Helper->storeChange(delta, db, limitsV2Frame->mEntry);

        innerResult().success().details.action(ManageLimitsAction::CREATE);
        innerResult().success().details.id() = limitsV2Frame->getID();
        break;
    }
    case ManageLimitsAction::DELETE:
    {
        auto limitsV2FrameDelete = limitsV2Helper->loadLimits(mManageLimits.details.id(), db, &delta);
        if (!limitsV2FrameDelete) {
            innerResult().code(ManageLimitsResultCode::NOT_FOUND);
            return false;
        }
        limitsV2Helper->storeDelete(delta, db, limitsV2FrameDelete->getKey());
        break;
    }
    default:
        throw std::runtime_error("Unexpected manage limits action in doApply. "
                                 "Expected UPDATE or REMOVE");
    }
    app.getMetrics().NewMeter({"op-manage-limits", "success", "apply"}, "operation").Mark();

    return true;
}

bool
ManageLimitsOpFrame::doCheckValid(Application& app)
{
    if ((mManageLimits.details.action() == ManageLimitsAction::CREATE) &&
        !!mManageLimits.details.limitsCreateDetails().accountID &&
        !!mManageLimits.details.limitsCreateDetails().accountType)
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
    if (mManageLimits.details.limitsCreateDetails().dailyOut < 0)
        return false;
    if (mManageLimits.details.limitsCreateDetails().weeklyOut <
        mManageLimits.details.limitsCreateDetails().dailyOut)
        return false;
    if (mManageLimits.details.limitsCreateDetails().monthlyOut <
        mManageLimits.details.limitsCreateDetails().weeklyOut)
        return false;
    return mManageLimits.details.limitsCreateDetails().annualOut >=
           mManageLimits.details.limitsCreateDetails().monthlyOut;
}

}
