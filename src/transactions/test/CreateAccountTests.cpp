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

TEST_CASE("create account", "[tx][create_account]")
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
		auto checkAccountPolicies = [&app, &delta](AccountID accountID, LedgerVersion expectedAccountVersion,
												   int32 expectedPolicies)
		{
			auto accountFrame = AccountFrame::loadAccount(accountID, app.getDatabase(), &delta);
			REQUIRE(accountFrame);
			REQUIRE(accountFrame->getAccount().ext.v() == expectedAccountVersion);
			if (expectedPolicies != -1)
				REQUIRE(accountFrame->getAccount().policies == expectedPolicies);
		};
		SECTION("Can update created without policies")
		{
			applyCreateAccountTx(app, rootKP, account, rootSeq++, AccountType::NOT_VERIFIED, nullptr, &validReferrer);
			checkAccountPolicies(account.getPublicKey(), LedgerVersion::EMPTY_VERSION, -1);
			// change type of account not_verified -> general
			applyCreateAccountTx(app, rootKP, account, rootSeq++, AccountType::GENERAL, nullptr, &validReferrer);
			checkAccountPolicies(account.getPublicKey(), LedgerVersion::EMPTY_VERSION, static_cast<int32_t>(AccountPolicies::NO_PERMISSIONS));

			// can update account's policies no_permissions -> allow_to_create_user_via_api
			applyCreateAccountTx(app, rootKP, account, rootSeq++, AccountType::GENERAL, nullptr, &validReferrer,
								 CreateAccountResultCode::SUCCESS, static_cast<int32_t>(AccountPolicies::ALLOW_TO_CREATE_USER_VIA_API));
			checkAccountPolicies(account.getPublicKey(), LedgerVersion::EMPTY_VERSION,
								 static_cast<int32_t>(AccountPolicies::ALLOW_TO_CREATE_USER_VIA_API));
			// can remove
			applyCreateAccountTx(app, rootKP, account, rootSeq++, AccountType::GENERAL, nullptr, &validReferrer,
								 CreateAccountResultCode::SUCCESS, static_cast<int32_t>(AccountPolicies::NO_PERMISSIONS));
			checkAccountPolicies(account.getPublicKey(), LedgerVersion::EMPTY_VERSION, static_cast<int32_t>(AccountPolicies::NO_PERMISSIONS));
		}
		
		SECTION("Can create account with policies")
		{
			applyCreateAccountTx(app, rootKP, account, rootSeq++, AccountType::GENERAL, nullptr, &validReferrer,
								 CreateAccountResultCode::SUCCESS, static_cast<int32_t>(AccountPolicies::ALLOW_TO_CREATE_USER_VIA_API));
			checkAccountPolicies(account.getPublicKey(), LedgerVersion::EMPTY_VERSION,
								 static_cast<int32_t>(AccountPolicies::ALLOW_TO_CREATE_USER_VIA_API));
		}
	}
	SECTION("Can't create account with non-zero policies and NON_VERYFIED type")
	{
		auto account = SecretKey::random();
		AccountID validReferrer = rootKP.getPublicKey();
		applyCreateAccountTx(app, rootKP, account, rootSeq++, AccountType::NOT_VERIFIED, nullptr, &validReferrer,
							 CreateAccountResultCode::TYPE_NOT_ALLOWED, 1);
	}
	SECTION("Root account can create account")
	{
		auto account = SecretKey::random();
        
        SECTION("referrer not found")
        {
            AccountID invalidReferrer = SecretKey::random().getPublicKey();
            applyCreateAccountTx(app, rootKP, account, rootSeq++, AccountType::GENERAL, nullptr, &invalidReferrer);
			auto accountFrame = AccountFrame::loadAccount(account.getPublicKey(), app.getDatabase());
			REQUIRE(accountFrame);
			REQUIRE(!accountFrame->getReferrer());
        }
        
        AccountID validReferrer = rootKP.getPublicKey();
		auto feeFrame = FeeFrame::create(FeeType::REFERRAL_FEE, 0, int64_t(0.5*ONE), app.getBaseAsset());
		auto fee = feeFrame->getFee();
		applySetFees(app, rootKP, 0, &fee, false, nullptr);
		applyCreateAccountTx(app, rootKP, account, rootSeq++, AccountType::GENERAL, nullptr, &validReferrer);
		SECTION("Requires med threshold")
		{
			ThresholdSetter th;
			th.masterWeight = make_optional<uint8_t>(100);
			th.lowThreshold = make_optional<uint8_t>(10);
			th.medThreshold = make_optional<uint8_t>(50);
			th.highThreshold = make_optional<uint8_t>(100);
			auto s1KP = SecretKey::random();
			auto s1 = Signer(s1KP.getPublicKey(), *th.medThreshold.get() - 1, static_cast<int32_t >(SignerType::GENERAL_ACC_MANAGER),
							 1, "", Signer::_ext_t{});
			applySetOptions(app, rootKP, rootSeq++, &th, &s1);
			auto createAccount = createCreateAccountTx(app.getNetworkID(), rootKP, account, rootSeq++, AccountType::GENERAL);
			createAccount->getEnvelope().signatures.clear();
			createAccount->addSignature(s1KP);
			REQUIRE(!applyCheck(createAccount, delta, app));
			REQUIRE(getFirstResultCode(*createAccount) == OperationResultCode::opBAD_AUTH);
		}
		SECTION("Root can create GENERAL account only with account creator signer")
		{
			auto root = loadAccount(rootKP, app);
			auto s1KP = SecretKey::random();
			auto signerTypes = xdr::xdr_traits<SignerType>::enum_values();
			for (auto signerType = signerTypes.begin();
                signerType != signerTypes.end(); ++signerType)
			{
				auto s1 = Signer(s1KP.getPublicKey(), root->getMediumThreshold() + 1, static_cast<int32_t >(*signerType),
								 1, "", Signer::_ext_t{});
				applySetOptions(app, rootKP, rootSeq++, nullptr, &s1);
				account = SecretKey::random();
				auto createAccount = createCreateAccountTx(app.getNetworkID(), rootKP, account, rootSeq++, AccountType::GENERAL);
				createAccount->getEnvelope().signatures.clear();
				createAccount->addSignature(s1KP);
				auto mustApply = *signerType == static_cast<int32_t >(SignerType::GENERAL_ACC_MANAGER) ||
								 *signerType == static_cast<int32_t >(SignerType::NOT_VERIFIED_ACC_MANAGER);
                LedgerDelta delta1(app.getLedgerManager().getCurrentLedgerHeader(), app.getDatabase());
                REQUIRE(mustApply == applyCheck(createAccount, delta1, app));
			}
		}
   }
	SECTION("Can change type of account and can change KYC and limits")
	{
        Limits limits;
        AccountType accountType = AccountType::NOT_VERIFIED;
        limits.dailyOut = 100;
        limits.weeklyOut = 300;
        limits.monthlyOut = 300;
        limits.annualOut = 300;
        applySetLimits(app, rootKP, rootSeq++, nullptr, &accountType, limits);


		auto newAccount = SecretKey::random();
		auto createAccount = createCreateAccountTx(app.getNetworkID(), rootKP, newAccount, rootSeq++, AccountType::NOT_VERIFIED);
		REQUIRE(applyCheck(createAccount, delta, app));
		auto newAccountFrame = AccountFrame::loadAccount(newAccount.getPublicKey(), app.getDatabase());
                

		REQUIRE(newAccountFrame);
		REQUIRE(newAccountFrame->getAccountType() == AccountType::NOT_VERIFIED);

		applyCreateAccountTx(app, rootKP, newAccount, rootSeq++, AccountType::GENERAL);
		newAccountFrame = AccountFrame::loadAccount(newAccount.getPublicKey(), app.getDatabase());
		REQUIRE(newAccountFrame->getAccountType() == AccountType::GENERAL);

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
			auto createAccount = createCreateAccountTx(app.getNetworkID(), accountCreator, toBeCreated, accountCreatorSeq++, AccountType::GENERAL);
			applyCheck(createAccount, delta, app);
			REQUIRE(getFirstResult(*createAccount).code() == OperationResultCode::opNOT_ALLOWED);
		}
	}
        SECTION("Can update not verified to syndicate")
	{
            auto toBeCreated = SecretKey::random();
            applyCreateAccountTx(app, rootKP, toBeCreated, 0, AccountType::NOT_VERIFIED);
            applyCreateAccountTx(app, rootKP, toBeCreated, 0, AccountType::SYNDICATE);
	}
	SECTION("Can only change account type from Not verified to general")
	{
		for (auto accountType : getAllAccountTypes())
		{
			// can be created only once
			if (isSystemAccountType(AccountType(accountType)))
				continue;

			for (auto updateAccountType : getAllAccountTypes())
			{
				if (isSystemAccountType(AccountType(updateAccountType)))
					continue;
                                if (updateAccountType == accountType)
                                    continue;
                                const auto isAllowedToUpdateTo = updateAccountType == AccountType::GENERAL || updateAccountType == AccountType::SYNDICATE;
				if (isAllowedToUpdateTo && accountType == AccountType::NOT_VERIFIED)
					continue;
                                auto toBeCreated = SecretKey::random();
                                applyCreateAccountTx(app, rootKP, toBeCreated, 0, AccountType(accountType));
			        applyCreateAccountTx(app, rootKP, toBeCreated, 0, updateAccountType, nullptr, nullptr,
									 CreateAccountResultCode::TYPE_NOT_ALLOWED);
			}
		}
	}
   

}
