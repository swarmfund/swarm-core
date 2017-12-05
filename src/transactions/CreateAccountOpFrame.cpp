// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/CreateAccountOpFrame.h"
#include "ledger/LedgerDelta.h"
#include "database/Database.h"

#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"

namespace stellar
{

using namespace std;
using xdr::operator==;
    
CreateAccountOpFrame::CreateAccountOpFrame(Operation const& op,
                                           OperationResult& res,
                                           TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mCreateAccount(mOperation.body.createAccountOp())
{
}

std::unordered_map<AccountID, CounterpartyDetails> CreateAccountOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
{
	return { 
		{mCreateAccount.destination, CounterpartyDetails({AccountType::NOT_VERIFIED, AccountType::GENERAL}, true, false) }
	};
}

SourceDetails CreateAccountOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const
{
	int32_t threshold = mSourceAccount->getMediumThreshold();
	uint32_t allowedSignerClass = 0;
	switch (mCreateAccount.accountType)
	{
	case AccountType::NOT_VERIFIED:
		allowedSignerClass = static_cast<int32_t >(SignerType::NOT_VERIFIED_ACC_MANAGER);
		break;
	case AccountType::GENERAL:
	case AccountType::SYNDICATE:
		if (mCreateAccount.policies != 0)
		{
			allowedSignerClass = static_cast<int32_t >(SignerType::GENERAL_ACC_MANAGER);
			break;
		}
		allowedSignerClass = static_cast<int32_t >(SignerType::GENERAL_ACC_MANAGER) |
									static_cast<int32_t >(SignerType::NOT_VERIFIED_ACC_MANAGER);
		break;
	default:
		// it is not allowed to create or update any other account types
		allowedSignerClass = 0;
		break;
	}
	return SourceDetails({ AccountType::MASTER }, threshold, allowedSignerClass);
}

void CreateAccountOpFrame::trySetReferrer(Application& app, Database& db, AccountFrame::pointer destAccountFrame)
{
	destAccountFrame->setShareForReferrer(0);
	if (!mCreateAccount.referrer)
	{
		return;
	}

	AccountID referrerAccountID = *mCreateAccount.referrer;
	if (referrerAccountID == app.getMasterID())
		return;

	auto referrer = AccountFrame::loadAccount(referrerAccountID, db);
	if (!referrer)
		return;

	// amount is not applyable for referral fee
	auto referralFeeFrame = FeeFrame::loadForAccount(FeeType::REFERRAL_FEE,
		app.getBaseAsset(), FeeFrame::SUBTYPE_ANY, referrer, 0, db);

	int64_t referralFee = referralFeeFrame ? referralFeeFrame->getPercentFee() : 0;
	destAccountFrame->setShareForReferrer(referralFee);
	destAccountFrame->setReferrer(*mCreateAccount.referrer);
}


bool CreateAccountOpFrame::createAccount(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager)
{
	Database& db = app.getDatabase();
	auto destAccountFrame = make_shared<AccountFrame>(mCreateAccount.destination);
	auto& destAccount = destAccountFrame->getAccount();

	destAccount.accountType = mCreateAccount.accountType;
	trySetReferrer(app, db, destAccountFrame);
	destAccount.policies = mCreateAccount.policies;

	destAccountFrame->storeAdd(delta, db);

	AccountManager accountManager(app, db, delta, ledgerManager);
	accountManager.createStats(destAccountFrame);
	// create balance for all availabe assets
	std::vector<AssetFrame::pointer> assets;
	AssetFrame::loadAssets(assets, db);
	for (auto baseAsset : assets) {
		BalanceID balanceID = mCreateAccount.destination;
		if (!(baseAsset->getCode() == app.getBaseAsset()))
			balanceID = BalanceKeyUtils::forAccount(mCreateAccount.destination, delta.getHeaderFrame().generateID());
		auto balanceFrame = BalanceFrame::createNew(balanceID, mCreateAccount.destination, baseAsset->getCode(), ledgerManager.getCloseTime());
		balanceFrame->storeAdd(delta, db);
	}

	innerResult().success().referrerFee = destAccountFrame->getShareForReferrer();
	app.getMetrics().NewMeter({ "op-create-account", "success", "apply" },
		"operation").Mark();
	return true;
}

bool
CreateAccountOpFrame::doApply(Application& app,
                              LedgerDelta& delta, LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();
	innerResult().code(CreateAccountResultCode::SUCCESS);

    auto destAccountFrame = AccountFrame::loadAccount(delta, mCreateAccount.destination, db);

	if (ledgerManager.getCurrentLedgerHeader().ledgerVersion < static_cast<int32_t >(mCreateAccount.ext.v()))
	{
		app.getMetrics().NewMeter({ "op-create-account", "invalid", "invalid_account_version" }, "operation").Mark();
		innerResult().code(CreateAccountResultCode::INVALID_ACCOUNT_VERSION);
		return false;
	}

    if (!destAccountFrame)
    {
		return createAccount(app, delta, ledgerManager);
    } 
	
	auto accountType = destAccountFrame->getAccountType();
	// it is only allowed to change account type from not verified to general
	bool isChangingAccountType = accountType != mCreateAccount.accountType;
	bool isNotVerifiedToGeneral = accountType == AccountType::NOT_VERIFIED && mCreateAccount.accountType == AccountType::GENERAL;
	if (isChangingAccountType && !isNotVerifiedToGeneral)
	{
		app.getMetrics().NewMeter({ "op-create-account", "invalid", "account-type-not-allowed" }, "operation").Mark();
		innerResult().code(CreateAccountResultCode::TYPE_NOT_ALLOWED);
		return false;
	}

	destAccountFrame->setAccountType(mCreateAccount.accountType);
	AccountEntry& accountEntry = destAccountFrame->getAccount();
	accountEntry.policies = mCreateAccount.policies;

	destAccountFrame->storeChange(delta, db);

	innerResult().success().referrerFee = destAccountFrame->getShareForReferrer();
	app.getMetrics().NewMeter({"op-create-account", "success", "apply"},
	                          "operation").Mark();
	return true;
}

bool
CreateAccountOpFrame::doCheckValid(Application& app)
{
    if (mCreateAccount.destination == getSourceID())
    {
        app.getMetrics().NewMeter({"op-create-account", "invalid",
                          "malformed-destination-equals-source"},
                         "operation").Mark();
        innerResult().code(CreateAccountResultCode::MALFORMED);
        return false;
    }

	if (mCreateAccount.accountType == AccountType::NOT_VERIFIED && mCreateAccount.policies != 0)
	{
		app.getMetrics().NewMeter({ "op-create-account", "invalid", "account-type-not-allowed" }, "operation").Mark();
		innerResult().code(CreateAccountResultCode::TYPE_NOT_ALLOWED);
		return false;
	}

	if (isSystemAccountType(mCreateAccount.accountType))
	{
		app.getMetrics().NewMeter({ "op-create-account", "invalid", "account-type-not-allowed" }, "operation").Mark();
		innerResult().code(CreateAccountResultCode::TYPE_NOT_ALLOWED);
		return false;
	}

    return true;
}
}
