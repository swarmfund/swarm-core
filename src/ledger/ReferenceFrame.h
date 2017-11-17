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

class ReferenceFrame : public EntryFrame
{
    static void
    loadPayments(StatementContext& prep,
               std::function<void(LedgerEntry const&)> paymentProcessor);

    ReferenceEntry& mPayment;

    ReferenceFrame(ReferenceFrame const& from);

    void storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert);

  public:
    typedef std::shared_ptr<ReferenceFrame> pointer;

    ReferenceFrame();
    ReferenceFrame(LedgerEntry const& from);

    ReferenceFrame& operator=(ReferenceFrame const& other);

    EntryFrame::pointer
    copy() const override
    {
        return EntryFrame::pointer(new ReferenceFrame(*this));
    }

    ReferenceEntry const&
    getPayment() const
    {
        return mPayment;
    }
    ReferenceEntry&
    getPayment()
    {
        return mPayment;
    }

    static bool isValid(ReferenceEntry const& oe);
    bool isValid() const;

    // Instance-based overrides of EntryFrame.
    void storeDelete(LedgerDelta& delta, Database& db) const override;
    void storeChange(LedgerDelta& delta, Database& db) override;
    void storeAdd(LedgerDelta& delta, Database& db) override;

    // Static helpers that don't assume an instance.
    static void storeDelete(LedgerDelta& delta, Database& db,
                            LedgerKey const& key);
	static bool exists(Database& db, LedgerKey const& key);
	static bool exists(Database& db, std::string reference, AccountID exchange);
    static uint64_t countObjects(soci::session& sess);

    // database utilities
    static pointer loadPayment(AccountID exchange, std::string reference,
                             Database& db, LedgerDelta* delta = nullptr);



    static void dropAll(Database& db);
    static const char* kSQLCreateStatement1;
    static const char* kSQLCreateStatement2;
};
}
