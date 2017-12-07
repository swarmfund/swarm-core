#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryFrame.h"

namespace soci
{
class session;
}

namespace stellar
{
class StatementContext;

class ExternalSystemAccountIDFrame : public EntryFrame
{

    ExternalSystemAccountID& mExternalSystemAccountID;

    ExternalSystemAccountIDFrame(ExternalSystemAccountIDFrame const& from);

public:
    typedef std::shared_ptr<ExternalSystemAccountIDFrame> pointer;

    ExternalSystemAccountIDFrame();
    ExternalSystemAccountIDFrame(LedgerEntry const& from);

    ExternalSystemAccountIDFrame& operator=(
        ExternalSystemAccountIDFrame const& other);

    EntryFrame::pointer copy() const override
    {
        return EntryFrame::pointer(new ExternalSystemAccountIDFrame(*this));
    }

    static pointer createNew(AccountID const accountID, ExternalSystemType const externalSystemType, std::string const data);

    ExternalSystemAccountID const& getExternalSystemAccountID() const
    {
        return mExternalSystemAccountID;
    }

    static bool isValid(ExternalSystemAccountID const& oe);
    bool isValid() const;

    // Instance-based overrides of EntryFrame.
    void storeDelete(LedgerDelta& delta, Database& db) const override;
    void storeChange(LedgerDelta& delta, Database& db) override;
    void storeAdd(LedgerDelta& delta, Database& db) override;

    // Static helpers that don't assume an instance.
    static void storeDelete(LedgerDelta& delta, Database& db,
                            LedgerKey const& key);
    static bool exists(Database& db, LedgerKey const& key);
    static bool exists(Database& db, AccountID accountID,
                       ExternalSystemType externalSystemType);
    static uint64_t countObjects(soci::session& sess);

    static void dropAll(Database& db);

    // load - loads external system account ID by accountID and externalSystemType. If not found returns nullptr.
    static pointer load(const AccountID accountID, const ExternalSystemType externalSystemType, Database& db, LedgerDelta* delta = nullptr);

private:
    static const char* select;

    void storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert);
    static void
        load(StatementContext& prep, std::function<void(LedgerEntry const&)> processor);
};
}
