// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0
#include "main/Application.h"
#include "util/Timer.h"
#include "main/Config.h"
#include "overlay/LoopbackPeer.h"
#include "main/test.h"
#include "lib/catch.hpp"
#include "TxTests.h"
#include "ledger/LedgerManager.h"
#include "ledger/LedgerDelta.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

TEST_CASE("create account", "[dep_tx][create_account]")
{
    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);

    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();
    closeLedgerOn(app, 2, 1, 7, 2014);

	upgradeToCurrentLedgerVersion(app);

	LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(),
		app.getDatabase());

    // set up world
    SecretKey rootKP = getRoot();
	Salt rootSeq = 1;

	SECTION("Can't create system account")
	{
		for (auto systemAccountType : getSystemAccountTypes())
		{
			auto randomAccount = SecretKey::random();
			auto createAccount = createCreateAccountTx(app.getNetworkID(), rootKP, randomAccount, 0, systemAccountType);
			applyCheck(createAccount, delta, app);
			REQUIRE(getFirstResult(*createAccount).code() == OperationResultCode::opNOT_ALLOWED);
		}
	}

	SECTION("Can set and update account policies")
	{
		auto account = SecretKey::random();
		AccountID validReferrer = rootKP.getPublicKey();
		auto checkAccountPolicies = [&app, &delta](AccountID accountID, int32 expectedAccountVersion, int32 expectedPolicies) {
			auto accountFrame = AccountFrame::loadAccount(accountID, app.getDatabase(), &delta);
			REQUIRE(accountFrame);
			REQUIRE(accountFrame->getAccount().ext.v() == expectedAccountVersion);
			if (expectedAccountVersion != LedgerVersion::ACCOUNT_POLICIES)
			{
				if (expectedPolicies != -1)
					throw std::invalid_argument("Invalid expected policies for provided expected account version");
				return;
			}

			REQUIRE(accountFrame->getAccount().ext.policies() == expectedPolicies);
		};
		SECTION("Can update created without policies")
		{
			applyCreateAccountTx(app, rootKP, account, rootSeq++, GENERAL, nullptr, &validReferrer);
			checkAccountPolicies(account.getPublicKey(), 0, -1);
			// can update account without polices
			applyCreateAccountTx(app, rootKP, account, rootSeq++, GENERAL, nullptr, &validReferrer, CREATE_ACCOUNT_SUCCESS,
				AccountPolicies::ALLOW_TO_CREATE_USER_VIA_API);
			checkAccountPolicies(account.getPublicKey(), LedgerVersion::ACCOUNT_POLICIES, AccountPolicies::ALLOW_TO_CREATE_USER_VIA_API);

			// can update account without changing policies
			applyCreateAccountTx(app, rootKP, account, rootSeq++, GENERAL, nullptr, &validReferrer);
			checkAccountPolicies(account.getPublicKey(), LedgerVersion::ACCOUNT_POLICIES, AccountPolicies::ALLOW_TO_CREATE_USER_VIA_API);

			// can update account with policies
			applyCreateAccountTx(app, rootKP, account, rootSeq++, GENERAL, nullptr, &validReferrer, CREATE_ACCOUNT_SUCCESS,
				AccountPolicies::ALLOW_TO_CREATE_USER_VIA_API);
			checkAccountPolicies(account.getPublicKey(), LedgerVersion::ACCOUNT_POLICIES, AccountPolicies::ALLOW_TO_CREATE_USER_VIA_API);

			// can remove
			applyCreateAccountTx(app, rootKP, account, rootSeq++, GENERAL, nullptr, &validReferrer, CREATE_ACCOUNT_SUCCESS,
				AccountPolicies::NO_PERMISSIONS);
			checkAccountPolicies(account.getPublicKey(), LedgerVersion::ACCOUNT_POLICIES, AccountPolicies::NO_PERMISSIONS);
		}
		
		SECTION("Can create account with policies")
		{
			applyCreateAccountTx(app, rootKP, account, rootSeq++, GENERAL, nullptr, &validReferrer, CREATE_ACCOUNT_SUCCESS,
				AccountPolicies::ALLOW_TO_CREATE_USER_VIA_API);
			checkAccountPolicies(account.getPublicKey(), LedgerVersion::ACCOUNT_POLICIES, AccountPolicies::ALLOW_TO_CREATE_USER_VIA_API);
		}
	}
	SECTION("Can't create account with non-zero policies and NON_VERYFIED type")
	{
		auto account = SecretKey::random();
		AccountID validReferrer = rootKP.getPublicKey();
		applyCreateAccountTx(app, rootKP, account, rootSeq++, NOT_VERIFIED, nullptr, &validReferrer, CREATE_ACCOUNT_TYPE_NOT_ALLOWED, 1);
	}
	SECTION("Root account can create account")
	{
		auto account = SecretKey::random();
        
        SECTION("referrer not found")
        {
            AccountID invalidReferrer = SecretKey::random().getPublicKey();
            applyCreateAccountTx(app, rootKP, account, rootSeq++, GENERAL, nullptr, &invalidReferrer);
			auto accountFrame = AccountFrame::loadAccount(account.getPublicKey(), app.getDatabase());
			REQUIRE(accountFrame);
			REQUIRE(!accountFrame->getReferrer());
        }
        
        AccountID validReferrer = rootKP.getPublicKey();
		auto feeFrame = FeeFrame::create(REFERRAL_FEE, 0, int64_t(0.5*ONE), app.getBaseAsset());
		auto fee = feeFrame->getFee();
		applySetFees(app, rootKP, 0, &fee, false, nullptr);
		applyCreateAccountTx(app, rootKP, account, rootSeq++, GENERAL, nullptr, &validReferrer);
		SECTION("Requires med threshold")
		{
			ThresholdSetter th;
			th.masterWeight = make_optional<uint8_t>(100);
			th.lowThreshold = make_optional<uint8_t>(10);
			th.medThreshold = make_optional<uint8_t>(50);
			th.highThreshold = make_optional<uint8_t>(100);
			auto s1KP = SecretKey::random();
			auto s1 = Signer(s1KP.getPublicKey(), (*th.medThreshold.get()) - 1, SIGNER_GENERAL_ACC_MANAGER, 1, Signer::_ext_t{});
			applySetOptions(app, rootKP, rootSeq++, &th, &s1);
			auto createAccount = createCreateAccountTx(app.getNetworkID(), rootKP, account, rootSeq++, GENERAL);
			createAccount->getEnvelope().signatures.clear();
			createAccount->addSignature(s1KP);
			REQUIRE(!applyCheck(createAccount, delta, app));
			REQUIRE(getFirstResultCode(*createAccount) == opBAD_AUTH);
		}
		SECTION("Root can create GENERAL account only with account creator signer")
		{
			auto root = loadAccount(rootKP, app);
			auto s1KP = SecretKey::random();
			auto signerTypes = xdr::xdr_traits<SignerType>::enum_values();
			for (auto signerType = signerTypes.begin();
                signerType != signerTypes.end(); ++signerType)
			{
				auto s1 = Signer(s1KP.getPublicKey(), root->getMediumThreshold() + 1, *signerType, 1, Signer::_ext_t{});
				applySetOptions(app, rootKP, rootSeq++, nullptr, &s1);
				account = SecretKey::random();
				auto createAccount = createCreateAccountTx(app.getNetworkID(), rootKP, account, rootSeq++, GENERAL);
				createAccount->getEnvelope().signatures.clear();
				createAccount->addSignature(s1KP);
				auto mustApply = *signerType == SIGNER_GENERAL_ACC_MANAGER || *signerType == SIGNER_NOT_VERIFIED_ACC_MANAGER;
                LedgerDelta delta1(app.getLedgerManager().getCurrentLedgerHeader(), app.getDatabase());
                REQUIRE(mustApply == applyCheck(createAccount, delta1, app));
			}
		}
   }
	SECTION("Can change type of account and can change KYC and limits")
	{
        Limits limits;
        AccountType accountType = NOT_VERIFIED;
        limits.dailyOut = 100;
        limits.weeklyOut = 300;
        limits.monthlyOut = 300;
        limits.annualOut = 300;
        applySetLimits(app, rootKP, rootSeq++, nullptr, &accountType, limits);


		auto newAccount = SecretKey::random();
		auto createAccount = createCreateAccountTx(app.getNetworkID(), rootKP, newAccount, rootSeq++, NOT_VERIFIED);
		REQUIRE(applyCheck(createAccount, delta, app));
		auto newAccountFrame = AccountFrame::loadAccount(newAccount.getPublicKey(), app.getDatabase());
                

		REQUIRE(newAccountFrame);
		REQUIRE(newAccountFrame->getAccountType() == NOT_VERIFIED);

		applyCreateAccountTx(app, rootKP, newAccount, rootSeq++, GENERAL);
		newAccountFrame = AccountFrame::loadAccount(newAccount.getPublicKey(), app.getDatabase());
		REQUIRE(newAccountFrame->getAccountType() == GENERAL);

	}
	SECTION("Non root account can't create")
	{
		for (auto accountType : xdr::xdr_traits<AccountType>::enum_values())
		{
			// can be created only once
			if (isSystemAccountType(AccountType(accountType)))
				continue;

			auto accountCreator = SecretKey::random();
			applyCreateAccountTx(app, rootKP, accountCreator, rootSeq++, AccountType(accountType));
			auto accountCreatorSeq = 1;
			auto toBeCreated = SecretKey::random();
			auto createAccount = createCreateAccountTx(app.getNetworkID(), accountCreator, toBeCreated, accountCreatorSeq++, GENERAL);
			applyCheck(createAccount, delta, app);
			REQUIRE(getFirstResult(*createAccount).code() == OperationResultCode::opNOT_ALLOWED);
		}
	}
	SECTION("Can only change account type from Not verified to general")
	{
		for (auto accountType : getAllAccountTypes())
		{
			// can be created only once
			if (isSystemAccountType(AccountType(accountType)))
				continue;

			
			auto toBeCreated = SecretKey::random();
			applyCreateAccountTx(app, rootKP, toBeCreated, 0, AccountType(accountType));
			for (auto updateAccountType : getAllAccountTypes())
			{
				if (isSystemAccountType(AccountType(updateAccountType)))
					continue;
				if (updateAccountType == GENERAL && accountType == NOT_VERIFIED || updateAccountType == accountType)
					continue;
				applyCreateAccountTx(app, rootKP, toBeCreated, 0, updateAccountType, nullptr, nullptr, CREATE_ACCOUNT_TYPE_NOT_ALLOWED);
			}
		}
	}
   

}
