#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryFrame.h"
#include <functional>
#include <unordered_map>

namespace soci
{
class session;
}

namespace stellar
{
class StatementContext;

class CoinsEmissionRequestFrame : public EntryFrame
{
    static void
    loadCoinsEmissionRequests(StatementContext& prep,
               std::function<void(LedgerEntry const&)> coinsEmissionRequestProcessor);

    CoinsEmissionRequestEntry& mCoinsEmissionRequest;

    CoinsEmissionRequestFrame(CoinsEmissionRequestFrame const& from);

    void storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert);

  public:
    typedef std::shared_ptr<CoinsEmissionRequestFrame> pointer;

    CoinsEmissionRequestFrame();
    CoinsEmissionRequestFrame(LedgerEntry const& from);

    CoinsEmissionRequestFrame& operator=(CoinsEmissionRequestFrame const& other);

    EntryFrame::pointer
    copy() const override
    {
        return EntryFrame::pointer(new CoinsEmissionRequestFrame(*this));
    }

    CoinsEmissionRequestEntry const&
    getCoinsEmissionRequest() const
    {
        return mCoinsEmissionRequest;
    }
    CoinsEmissionRequestEntry&
    getCoinsEmissionRequest()
    {
        return mCoinsEmissionRequest;
    }

    AssetCode
    getAsset()
    {
        return mCoinsEmissionRequest.asset;
    }
    
    int64
    getAmount()
    {
        return mCoinsEmissionRequest.amount;
    }

    BalanceID
    getReceiver()
    {
        return mCoinsEmissionRequest.receiver;
    }

    AccountID
    getIssuer()
    {
        return mCoinsEmissionRequest.issuer;
    }

    uint64_t
    getID()
    {
        return mCoinsEmissionRequest.requestID;
    }

    std::string
    getReference()
    {
        return mCoinsEmissionRequest.reference;
    }

    bool
    getIsApproved()
    {
        return mCoinsEmissionRequest.isApproved;
    }

    void
    setIsApproved(bool approved)
    {
        mCoinsEmissionRequest.isApproved = approved;
    }

    static bool isValid(CoinsEmissionRequestEntry const& oe);
    bool isValid() const;

    static CoinsEmissionRequestFrame::pointer create(int64_t amount, AssetCode asset,
        std::string reference, uint64_t requestID, AccountID issuer,
        AccountID receiver);


    // Instance-based overrides of EntryFrame.
    void storeDelete(LedgerDelta& delta, Database& db) const override;
    void storeChange(LedgerDelta& delta, Database& db) override;
    void storeAdd(LedgerDelta& delta, Database& db) override;

    // Static helpers that don't assume an instance.
    static void storeDelete(LedgerDelta& delta, Database& db,
                            LedgerKey const& key);

    static void deleteForIssuer(LedgerDelta& delta, Database& db, AccountID issuer);

	static bool exists(Database& db, LedgerKey const& key);
	static bool exists(Database& db, uint64 requestID);
	static bool exists(Database& db, AccountID issuer, std::string reference);
    static uint64_t countObjects(soci::session& sess);

    // database utilities
    static pointer loadCoinsEmissionRequest(uint64 requestID,
                             Database& db, LedgerDelta* delta = nullptr);

    static void loadCoinsEmissionRequests(AccountID const& accountID,
                           std::vector<CoinsEmissionRequestFrame::pointer>& retCoinsEmissionRequests,
                           Database& db);

    // load all coinsEmissionRequests from the database (very slow)
    static std::unordered_map<AccountID, std::vector<CoinsEmissionRequestFrame::pointer>>
    loadAllCoinsEmissionRequests(Database& db);

    static void dropAll(Database& db);
    static const char* kSQLCreateStatement1;
    static const char* kSQLCreateStatement2;
};
}
