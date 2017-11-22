// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/test_helper/TestManager.h"
#include "transactions/TransactionFrame.h"
#include "herder/TxSetFrame.h"
#include "herder/LedgerCloseData.h"
#include "ledger/LedgerManager.h"
#include "ledger/LedgerDelta.h"
#include "util/Timer.h"
#include "invariant/Invariants.h"


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

		LedgerUpgrade upgrade(LEDGER_UPGRADE_VERSION);
		upgrade.newLedgerVersion() = mApp.getConfig().LEDGER_PROTOCOL_VERSION;
		xdr::xvector<UpgradeType, 6> upgrades;
		Value v(xdr::xdr_to_opaque(upgrade));
		upgrades.emplace_back(v.begin(), v.end());

		StellarValue sv(txSet->getContentsHash(), 1, upgrades, StellarValue::_ext_t(LedgerVersion::EMPTY_VERSION));
		LedgerCloseData ledgerData(1, txSet, sv);
		mLm.closeLedger(ledgerData);
	}

	bool TestManager::apply(TransactionFramePtr tx)
	{
		tx->clearCached();
		bool isTxValid = tx->checkValid(mApp);
		auto validationResult = tx->getResult();
		checkResult(validationResult, isTxValid);

		const auto code = validationResult.result.code();
		if (code == txDUPLICATION) {
			return false;
		}

		if (code != txNO_ACCOUNT)
		{
			tx->processSeqNum();
		}

		LedgerDelta txDelta(mDelta);
		bool isApplied = tx->apply(txDelta, mApp);
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
			REQUIRE(result.result.code() == txSUCCESS);
			return;
		}

		REQUIRE(result.result.code() != txSUCCESS);
	}

	bool TestManager::applyCheck(TransactionFramePtr tx)
	{
		const bool isApplied = apply(tx);
		// validates db state
		 mLm.checkDbState();
		auto txSet = std::make_shared<TxSetFrame>(mLm.getLastClosedLedgerHeader().hash);
		txSet->add(tx);
		mApp.getInvariants().check(txSet, mDelta);
		return isApplied;
	}
}

}
