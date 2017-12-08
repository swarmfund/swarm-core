// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/RecoverOpFrame.h"
#include "database/Database.h"
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"
#include "ledger/LedgerDelta.h"
#include "ledger/AccountHelper.h"
#include "util/types.h"

namespace stellar
{
using xdr::operator==;


std::unordered_map<AccountID, CounterpartyDetails> RecoverOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
{
	return{ 
		{ mRecover.account, CounterpartyDetails({AccountType::NOT_VERIFIED, AccountType::GENERAL}, true, true) }
	};
}

SourceDetails RecoverOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const
{
	if (counterpartiesDetails.size() != 1)
		throw std::invalid_argument("Unexpected counterparty details size");
	auto counterparty = counterpartiesDetails.find(mRecover.account);
	if (counterparty == counterpartiesDetails.end() || !counterparty->second.mAccount)
		throw std::invalid_argument("Expected only counterparty to be in map");

	
	uint32_t allowedSignerClass = 0;
	switch (counterparty->second.mAccount->getAccountType())
	{
	case AccountType::NOT_VERIFIED:
		allowedSignerClass = static_cast<int32_t>(SignerType::NOT_VERIFIED_ACC_MANAGER);
		break;
	case AccountType::GENERAL:
		allowedSignerClass = static_cast<int32_t>(SignerType::GENERAL_ACC_MANAGER);
		break;
	default:
		throw std::invalid_argument("Unexpected counterparty type in recovery");
	}
	return SourceDetails({AccountType::MASTER}, mSourceAccount->getLowThreshold(), allowedSignerClass);
}

RecoverOpFrame::RecoverOpFrame(Operation const& op, OperationResult& res,
                                     TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mRecover(mOperation.body.recoverOp())
{
}

bool
RecoverOpFrame::doApply(Application& app, LedgerDelta& delta,
                           LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();

	auto accountHelper = AccountHelper::Instance();
    AccountFrame::pointer targetAccountFrame = accountHelper->loadAccount(delta, mRecover.account, db);
	if (!targetAccountFrame)
	{
		app.getMetrics().NewMeter({ "op-recover", "failure", "targe-account-not-exists" }, "operation").Mark();
		innerResult().code(RecoverResultCode::MALFORMED);
		return false;
	}
    
	auto accountType = targetAccountFrame->getAccountType();
	if (accountType != AccountType::GENERAL && accountType != AccountType::NOT_VERIFIED)
	{
		app.getMetrics().NewMeter({ "op-recover", "failure", "invalid-account-type" }, "operation").Mark();
		innerResult().code(RecoverResultCode::MALFORMED);
		return false;
	}

	targetAccountFrame->setUpdateSigners(true);
	auto& targetAccount = targetAccountFrame->getAccount();
	targetAccount.thresholds[static_cast<int32_t>(ThresholdIndexes::MASTER_WEIGHT)] = 0;
    auto& signers = targetAccountFrame->getAccount().signers;
	signers.clear();
	Signer newSigner;
	newSigner.identity = 0;
	newSigner.pubKey = mRecover.newSigner;
	uint32_t newSignerWeight = targetAccount.thresholds[static_cast<int32_t>(ThresholdIndexes::HIGH)];
	newSigner.weight = newSignerWeight == 0 ? 1 : newSignerWeight;
	newSigner.signerType = getAnySignerType();
	signers.push_back(newSigner);

	targetAccount.blockReasons &= ~static_cast<int32_t>(BlockReasons::RECOVERY_REQUEST);
	EntryHelperProvider::storeChangeEntry(delta, db, targetAccountFrame->mEntry);

    app.getMetrics().NewMeter({"op-recover", "success", "apply"}, "operation")
        .Mark();
    innerResult().code(RecoverResultCode::SUCCESS);

	return true;
}

bool
RecoverOpFrame::doCheckValid(Application& app)
{
    if (mRecover.oldSigner == mRecover.newSigner || mRecover.account == mRecover.newSigner)
    {
        app.getMetrics().NewMeter({"op-recover", "invalid", "malformed-same-signer"},
                         "operation").Mark();
        innerResult().code(RecoverResultCode::MALFORMED);
        return false;
    }
    return true;
}

}
