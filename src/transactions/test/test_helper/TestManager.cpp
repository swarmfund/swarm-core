// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "TestManager.h"
#include "transactions/TransactionFrame.h"
#include "herder/TxSetFrame.h"
#include "herder/LedgerCloseData.h"
#include "ledger/LedgerManager.h"
#include "ledger/LedgerDelta.h"
#include "util/Timer.h"
#include "invariant/Invariants.h"
#include "test/test_marshaler.h"

namespace stellar
{

namespace txtest
{
	TestManager::TestManager(Application & app, Database & db, LedgerManager & lm) :
		mApp(app), mDB(db), mDelta(lm.getCurrentLedgerHeader(), db), mLm(lm)
	{
	}

	TestManager::pointer TestManager::make(Application & app)
	{
		Database& db = app.getDatabase();
		LedgerManager& lm = app.getLedgerManager();
		return std::make_shared<TestManager>(app, db, lm);
	}
	void TestManager::upgradeToCurrentLedgerVersion()
	{
		auto const& lcl = mLm.getLastClosedLedgerHeader();
		auto const& lastHash = lcl.hash;
		TxSetFramePtr txSet = std::make_shared<TxSetFrame>(lastHash);

		
		xdr::xvector<UpgradeType, 6> upgrades;
                auto ledgerVersion = this->ledgerVersion();
		upgrades.emplace_back(ledgerVersion.begin(), ledgerVersion.end());
                auto externalSystemGenerators = this->externalSystemGenerators();
                upgrades.emplace_back(externalSystemGenerators.begin(), externalSystemGenerators.end());

		StellarValue sv(txSet->getContentsHash(), 1, upgrades, StellarValue::_ext_t(LedgerVersion::EMPTY_VERSION));
		LedgerCloseData ledgerData(1, txSet, sv);
		mLm.closeLedger(ledgerData);
	}

bool TestManager::apply(TransactionFramePtr tx, std::vector<LedgerDelta::KeyEntryMap>& stateBeforeOp)
{
    tx->clearCached();
    bool isTxValid = tx->checkValid(mApp);
    auto validationResult = tx->getResult();
    checkResult(validationResult, isTxValid);

    const auto code = validationResult.result.code();
    if (code == TransactionResultCode::txDUPLICATION) {
        return false;
    }

    if (code != TransactionResultCode::txNO_ACCOUNT)
    {
        tx->processSeqNum();
    }

    LedgerDelta txDelta(mDelta);
    TransactionMeta txMeta;
    bool isApplied = tx->apply(txDelta, txMeta, mApp, stateBeforeOp);
    auto applyResult = tx->getResult();
    checkResult(applyResult, isApplied);

    if (!isTxValid)
    {
        REQUIRE(validationResult == applyResult);
    }

    if (isApplied)
    {
        txDelta.commit();
    }

    return isApplied;
}

void TestManager::checkResult(TransactionResult result, bool mustSuccess)
	{
		if (mustSuccess) {
			REQUIRE(result.result.code() == TransactionResultCode::txSUCCESS);
			return;
		}

		REQUIRE(result.result.code() != TransactionResultCode::txSUCCESS);
	}

Value TestManager::ledgerVersion() const
{
    LedgerUpgrade upgrade(LedgerUpgradeType::VERSION);
    upgrade.newLedgerVersion() = mApp.getConfig().LEDGER_PROTOCOL_VERSION;
    Value v(xdr::xdr_to_opaque(upgrade));
    return v;
}

Value TestManager::externalSystemGenerators()
{
    LedgerUpgrade upgrade(LedgerUpgradeType::EXTERNAL_SYSTEM_ID_GENERATOR);
    upgrade.newExternalSystemIDGenerators().push_back(ExternalSystemIDGeneratorType::BITCOIN_BASIC);
    upgrade.newExternalSystemIDGenerators().push_back(ExternalSystemIDGeneratorType::ETHEREUM_BASIC);
    Value v(xdr::xdr_to_opaque(upgrade));
    return v;
}

bool TestManager::applyCheck(TransactionFramePtr tx)
	{
    std::vector<LedgerDelta::KeyEntryMap> stateBeforeOp;
    return applyCheck(tx, stateBeforeOp);
	}

bool TestManager::applyCheck(TransactionFramePtr tx, std::vector<LedgerDelta::KeyEntryMap>& stateBeforeOp)
{
    const bool isApplied = apply(tx, stateBeforeOp);
    // validates db state
    mLm.checkDbState();
    auto txSet = std::make_shared<TxSetFrame>(mLm.getLastClosedLedgerHeader().hash);
    txSet->add(tx);
    mApp.getInvariants().check(txSet, mDelta);
    return isApplied;
}
}

}
