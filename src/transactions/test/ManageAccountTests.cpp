// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0
#include "main/Application.h"
#include "util/Timer.h"
#include "main/Config.h"
#include "overlay/LoopbackPeer.h"
#include "util/make_unique.h"
#include "main/test.h"
#include "TxTests.h"
#include "ledger/LedgerDeltaImpl.h"
#include "transactions/ManageAccountOpFrame.h"
#include "test/test_marshaler.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

TEST_CASE("Manage account", "[dep_tx][manage_account]")
{
    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);

    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();
    closeLedgerOn(app, 2, 1, 7, 2014);
	LedgerDeltaImpl delta(app.getLedgerManager().getCurrentLedgerHeader(),
		app.getDatabase());

    // set up world
    SecretKey rootKP = getRoot();
	Salt rootSeq = 1;
	auto account = SecretKey::random();
	applyCreateAccountTx(app, rootKP, account, rootSeq++, AccountType::GENERAL);

	SECTION("Common")
	{
		SECTION("Check source account requirements")
		{
			auto tx = createManageAccount(app.getNetworkID(), rootKP, account, rootSeq++, 0, 0);
			tx->checkValid(app);
			auto op = tx->getOperations()[0];
			op->checkValid(app, &delta);
			// Master is only source
			auto sourceDetails = op->getSourceAccountDetails({}, 0);
			auto allowedSources = sourceDetails.mAllowedSourceAccountTypes;
			REQUIRE(allowedSources.size() == 1);
			REQUIRE(allowedSources[0] == AccountType::MASTER);
			//Only account creator can sign
			auto requiredSigner = sourceDetails.mNeededSignedClass;
			REQUIRE(requiredSigner == (static_cast<int32_t >(SignerType::NOT_VERIFIED_ACC_MANAGER) |
									   static_cast<int32_t >(SignerType::GENERAL_ACC_MANAGER)));
		}
		SECTION("Account does not exists")
		{
			auto randomAccount = SecretKey::random();
			TransactionFramePtr txFrame = createManageAccount(app.getNetworkID(), rootKP, randomAccount, rootSeq++, 1, 0, AccountType::GENERAL);
            checkTransactionForOpResult(txFrame, app, OperationResultCode::opNO_COUNTERPARTY);
		}
		SECTION("Can't manage master")
		{
			auto tx = createManageAccount(app.getNetworkID(), rootKP, rootKP, rootSeq++, 0, 0, AccountType::MASTER);
			tx->checkValid(app);
			auto op = tx->getOperations()[0];
			op->checkValid(app, &delta);
			// Master is only source
			auto sourceDetails = op->getSourceAccountDetails({}, 0);
			auto allowedSources = sourceDetails.mAllowedSourceAccountTypes;
			REQUIRE(sourceDetails.mNeededSignedClass == 0);
		}
		SECTION("Success on empty manage account")
		{
			applyManageAccountTx(app, rootKP, account, rootSeq++, 0, 0, AccountType::GENERAL, ManageAccountResultCode::SUCCESS);
		}
		SECTION("Block and unblock the same flag should crash")
		{
			applyManageAccountTx(app, rootKP, account, rootSeq++, 1, 1, AccountType::GENERAL, ManageAccountResultCode::MALFORMED);
		}
	}
	SECTION("Block account")
	{
		// unblock not blocked
		applyManageAccountTx(app, rootKP, account, rootSeq++, 0, 1, AccountType::GENERAL);
		// account type mismatched
		applyManageAccountTx(app, rootKP, account, rootSeq++, 0, 1, AccountType::NOT_VERIFIED, ManageAccountResultCode::TYPE_MISMATCH);
		// block not blocked
		applyManageAccountTx(app, rootKP, account, rootSeq++, 1, 0, AccountType::GENERAL);
		// block blocked
		applyManageAccountTx(app, rootKP, account, rootSeq++, 1, 0, AccountType::GENERAL);
		// unblock
		applyManageAccountTx(app, rootKP, account, rootSeq++, 0, 1, AccountType::GENERAL);
	}

}
