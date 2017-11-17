// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/ManageBalanceOpFrame.h"
#include "ledger/LedgerDelta.h"
#include "database/Database.h"
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"

namespace stellar
{

using namespace std;
using xdr::operator==;
    
std::unordered_map<AccountID, CounterpartyDetails> ManageBalanceOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
{
	std::vector<AccountType> allowedCounterparties;
	if (getSourceID() == mManageBalance.destination)
		allowedCounterparties = { GENERAL, NOT_VERIFIED };
	else
		allowedCounterparties = { GENERAL, NOT_VERIFIED };
	return{
		{ mManageBalance.destination, CounterpartyDetails(allowedCounterparties, true, true)}
	};
}

SourceDetails ManageBalanceOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const
{
	std::vector<AccountType> allowedSourceAccounts;
	if (getSourceID() == mManageBalance.destination)
		allowedSourceAccounts = { GENERAL, NOT_VERIFIED};
	else
		allowedSourceAccounts = {};
	return SourceDetails(allowedSourceAccounts, mSourceAccount->getLowThreshold(), SIGNER_BALANCE_MANAGER);
}

ManageBalanceOpFrame::ManageBalanceOpFrame(Operation const& op,
                                           OperationResult& res,
                                           TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mManageBalance(mOperation.body.manageBalanceOp())
{
}

bool
ManageBalanceOpFrame::doApply(Application& app,
                              LedgerDelta& delta, LedgerManager& ledgerManager)
{
    AccountFrame::pointer destAccountFrame;

    Database& db = ledgerManager.getDatabase();
	destAccountFrame =
        AccountFrame::loadAccount(delta, mManageBalance.destination, db);
    if (!destAccountFrame)
    {
        app.getMetrics().NewMeter({ "op-manage-balance", "invalid", "dest-not-found" },
            "operation").Mark();
        innerResult().code(MANAGE_BALANCE_DESTINATION_NOT_FOUND);
        return false;
    }

	auto balanceFrame = BalanceFrame::loadBalance(mManageBalance.destination, mManageBalance.asset, db, &delta);

	if (balanceFrame || BalanceFrame::exists(db, mManageBalance.balanceID))
	{
		app.getMetrics().NewMeter({ "op-manage-balance", "invalid", "already-exists" },
			"operation").Mark();
		innerResult().code(MANAGE_BALANCE_ALREADY_EXISTS);
		return false;
	}

	auto assetFrame = AssetFrame::loadAsset(mManageBalance.asset, db);
	if (!assetFrame)
	{
		app.getMetrics().NewMeter({ "op-manage-balance", "invalid", "asset-not-found" },
			"operation").Mark();
		innerResult().code(MANAGE_BALANCE_ASSET_NOT_FOUND);
		return false;
	}

	balanceFrame = BalanceFrame::createNew(mManageBalance.balanceID, mManageBalance.destination, mManageBalance.asset, ledgerManager.getCloseTime());
	balanceFrame->storeAdd(delta, db);
    
	app.getMetrics().NewMeter({"op-manage-balance", "success", "apply"},
	                          "operation").Mark();
	innerResult().code(MANAGE_BALANCE_SUCCESS);
	return true;
}

bool
ManageBalanceOpFrame::doCheckValid(Application& app)
{
    if (mManageBalance.action == MANAGE_BALANCE_DELETE)
    {
        app.getMetrics().NewMeter({"op-manage-balance", "invalid",
                          "malformed-destination-for-delete"},
                         "operation").Mark();
        innerResult().code(MANAGE_BALANCE_MALFORMED);
        return false;
    }

    if (!isAssetValid(mManageBalance.asset))
    {
        app.getMetrics().NewMeter({"op-manage-asset", "invalid",
                          "malformed-invalid-asset"},
                         "operation").Mark();
        innerResult().code(MANAGE_BALANCE_INVALID_ASSET);
        return false;
    }


    return true;
}
}
