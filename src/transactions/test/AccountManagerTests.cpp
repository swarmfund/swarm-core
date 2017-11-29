// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "main/Application.h"
#include "main/Config.h"
#include "util/Timer.h"
#include "overlay/LoopbackPeer.h"
#include "util/make_unique.h"
#include "main/test.h"
#include "lib/catch.hpp"
#include "util/Logging.h"
#include "TxTests.h"
#include "transactions/TransactionFrame.h"
#include "ledger/LedgerDelta.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

// Try setting each option to make sure it works
// try setting all at once
// try setting high threshold ones without the correct sigs
// make sure it doesn't allow us to add signers when we don't have the
// minbalance
TEST_CASE("account_manager", "[dep_tx][account_manager]")
{
    using xdr::operator==;

    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);

    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();

    closeLedgerOn(app, 2, 1, 7, 2014);

    // set up world
    SecretKey root = getRoot();
    SecretKey issuance = getIssuanceKey();

    Salt rootSeq = 1;

	Limits limits(app.getConfig().EMISSION_UNIT, 2 * app.getConfig().EMISSION_UNIT, 4 * app.getConfig().EMISSION_UNIT, 4 * app.getConfig().EMISSION_UNIT, Limits::_ext_t(LedgerVersion::EMPTY_VERSION));
	AccountType accountType = AccountType::GENERAL;
    
    auto amount = app.getConfig().EMISSION_UNIT;

    applySetLimits(app, root, rootSeq++, nullptr, &accountType, limits);

    auto a = SecretKey::random();
    applyCreateAccountTx(app, root, a, rootSeq++, accountType);
    auto account = loadAccount(a, app);
	REQUIRE(account);

	LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(),
		app.getDatabase());
    AccountManager accountManager(app, app.getDatabase(), delta, app.getLedgerManager());
    auto now = app.getLedgerManager().getCloseTime();


    auto balance = BalanceFrame::loadBalance(a.getPublicKey(), app.getDatabase());

    int64 universalAmount;

    auto transferResult = accountManager.processTransfer(account, balance, amount,
        universalAmount);
    REQUIRE(transferResult == AccountManager::UNDERFUNDED);
    
 
    int64 emissionAmount = amount * 5;
    fundAccount(app, root, issuance, rootSeq, a.getPublicKey(), emissionAmount);
	fundAccount(app, root, issuance, rootSeq, a.getPublicKey(), emissionAmount);

    balance = BalanceFrame::loadBalance(a.getPublicKey(), app.getDatabase());
    transferResult = accountManager.processTransfer(account, balance, amount * 2,
        universalAmount);
    REQUIRE(universalAmount == amount * 2);
    REQUIRE(transferResult == AccountManager::LIMITS_EXCEEDED);

    balance = BalanceFrame::loadBalance(a.getPublicKey(), app.getDatabase());
    transferResult = accountManager.processTransfer(account, balance, amount,
        universalAmount);
    REQUIRE(transferResult == AccountManager::SUCCESS);
    
    
    transferResult = accountManager.processTransfer(account, balance, amount,
        universalAmount);
    REQUIRE(transferResult == AccountManager::LIMITS_EXCEEDED);
    
    // Monthly limit works and daily is cleared
    closeLedgerOn(app, 3, 3, 7, 2014);
    now = app.getLedgerManager().getCloseTime();
    balance = BalanceFrame::loadBalance(a.getPublicKey(), app.getDatabase());
    // weekly limit handled
    

    transferResult = accountManager.processTransfer(account, balance, amount * 3,
        universalAmount);
    REQUIRE(transferResult == AccountManager::LIMITS_EXCEEDED);

    closeLedgerOn(app, 4, 10, 7, 2014);
    now = app.getLedgerManager().getCloseTime();
    
    // Monthly limit works and daily/weekly are cleared
    transferResult = accountManager.processTransfer(account, balance, amount,
        universalAmount);
    REQUIRE(transferResult == AccountManager::SUCCESS);
    
    SECTION("revert without price change")
    {
        auto a2 = SecretKey::random();

        applyCreateAccountTx(app, root, a2, rootSeq++, accountType);
        auto balanceID = a2.getPublicKey();

        fundAccount(app, root, issuance, rootSeq, balanceID,
            emissionAmount);
        balance = BalanceFrame::loadBalance(balanceID, app.getDatabase());
        transferResult = accountManager.processTransfer(account, balance, amount,
            universalAmount, true);
        REQUIRE(transferResult == AccountManager::SUCCESS);
        
        REQUIRE(accountManager.revertRequest(account, balance, amount, amount, now));

        balance = BalanceFrame::loadBalance(balanceID, app.getDatabase());
        transferResult = accountManager.processTransfer(account, balance, amount,
            universalAmount, true);
        REQUIRE(transferResult == AccountManager::SUCCESS);

        REQUIRE(accountManager.revertRequest(account, balance, amount,
            amount, now));

        
        REQUIRE(balance->getLocked() == 0);
        REQUIRE(balance->getAmount() == emissionAmount);
    }
    
    
}
