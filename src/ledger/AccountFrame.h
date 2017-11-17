#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryFrame.h"
#include <functional>
#include "map"
#include <unordered_map>
#include "xdr/Stellar-ledger-entries-account.h"

namespace soci
{
class session;
namespace details
{
class prepare_temp_type;
}
}

namespace stellar
{
class LedgerManager;

class AccountFrame : public EntryFrame
{
    void storeUpdate(LedgerDelta& delta, Database& db, bool insert);
    bool mUpdateSigners;

    AccountEntry& mAccountEntry;

    void normalize();

    AccountFrame(AccountFrame const& from);

    bool isValid();

    static std::vector<Signer> loadSigners(Database& db,
                                           std::string const& actIDStrKey,
										   LedgerDelta* delta);

    void applySigners(Database& db, bool insert, LedgerDelta& delta);
	void deleteSigner(Database& db, std::string const& accountID, AccountID const& pubKey);
	void signerStoreChange(Database& db, LedgerDelta& delta, std::string const& accountID, std::vector<Signer>::iterator const& signer, bool insert);


  public:
    typedef std::shared_ptr<AccountFrame> pointer;

    AccountFrame();
    AccountFrame(LedgerEntry const& from);
    AccountFrame(AccountID const& id);

    // builds an accountFrame for the sole purpose of authentication
    static AccountFrame::pointer makeAuthOnlyAccount(AccountID const& id);

    EntryFrame::pointer
    copy() const override
    {
        return EntryFrame::pointer(new AccountFrame(*this));
    }

	static bool isLimitsValid(Limits const& limits)
	{
		if (limits.dailyOut < 0)
			return false;
		return limits.dailyOut <= limits.weeklyOut && limits.weeklyOut <= limits.monthlyOut && limits.monthlyOut <= limits.annualOut;
	}

    void
    setUpdateSigners()
    {
        normalize();
        mUpdateSigners = true;
    }

	bool isBlocked() const;
    void setBlockReasons(uint32 reasonsToAdd, uint32 reasonsToRemove) const;
    AccountID const& getID() const;

	bool checkPolicy(AccountPolicies policy) const
	{
		if (mAccountEntry.ext.v() != LedgerVersion::ACCOUNT_POLICIES)
			return false;

		return (mAccountEntry.ext.policies() & policy) == policy;
	}

    uint32_t getMasterWeight() const;
    uint32_t getHighThreshold() const;
    uint32_t getMediumThreshold() const;
    uint32_t getLowThreshold() const;

    AccountEntry const&
    getAccount() const
    {
        return mAccountEntry;
    }

    AccountEntry&
    getAccount()
    {
        clearCached();
        return mAccountEntry;
    }

	AccountType getAccountType() const
    {
		return mAccountEntry.accountType;
    }

	uint32 getBlockReasons() const
    {
		return mAccountEntry.blockReasons;
    }
    
    void setAccountType(AccountType accountType)
    {
        mAccountEntry.accountType = accountType;
    }

    void setReferrer(AccountID referrer)
    {
        mAccountEntry.referrer.activate() = referrer;
    }

	AccountID* getReferrer() const
    {
		return mAccountEntry.referrer.get();
    }

    void setShareForReferrer(int64 share)
    {
        mAccountEntry.shareForReferrer = share;
    }


    int64 getShareForReferrer()
    {
        return mAccountEntry.shareForReferrer;
    }

    // Instance-based overrides of EntryFrame.
    void storeDelete(LedgerDelta& delta, Database& db) const override;
    void storeChange(LedgerDelta& delta, Database& db) override;
    void storeAdd(LedgerDelta& delta, Database& db) override;
    
    // Static helper that don't assume an instance.
    static void storeDelete(LedgerDelta& delta, Database& db,
                            LedgerKey const& key);
    static bool exists(Database& db, LedgerKey const& key);
    static uint64_t countObjects(soci::session& sess);

    // database utilities
    static AccountFrame::pointer
    loadAccount(LedgerDelta& delta, AccountID const& accountID, Database& db);
    static AccountFrame::pointer loadAccount(AccountID const& accountID,
                                             Database& db, LedgerDelta* delta = nullptr);
	static AccountFrame::pointer
	mustLoadAccount(AccountID const& accountID, Database& db, LedgerDelta* delta = nullptr);

    // compare signers, ignores weight
    static bool signerCompare(Signer const& s1, Signer const& s2);

    // loads all accounts from database and checks for consistency (slow!)
    static std::unordered_map<AccountID, AccountFrame::pointer>
    checkDB(Database& db);

    static void dropAll(Database& db);
	static void addSignerName(Database& db);
	static void addSignerVersion(Database& db);
	static void addAccountPolicies(Database& db);
	static void addCreatedAt(Database& db);

    static const char* kSQLCreateStatement1;
    static const char* kSQLCreateStatement2;
    static const char* kSQLCreateStatement3;
	static const char* kSQLAddSignerName;
};
}
