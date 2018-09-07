#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryHelperLegacy.h"
#include "ledger/LedgerManager.h"
#include <functional>
#include "SaleFrame.h"

namespace soci
{
    class session;
}

namespace stellar
{
    class StatementContext;

    class SaleHelper : public EntryHelperLegacy {
    public:
        SaleHelper(SaleHelper const&) = delete;
        SaleHelper& operator= (SaleHelper const&) = delete;

        static SaleHelper *Instance() {
            static SaleHelper singleton;
            return&singleton;
        }

        void dropAll(Database& db) override;
        void addType(Database& db);
        void addState(Database& db);
        void storeAdd(LedgerDelta& delta, Database& db, LedgerEntry const& entry) override;
        void storeChange(LedgerDelta& delta, Database& db, LedgerEntry const& entry) override;
        void storeDelete(LedgerDelta& delta, Database& db, LedgerKey const& key) override;
        bool exists(Database& db, LedgerKey const& key) override;
        LedgerKey getLedgerKey(LedgerEntry const& from) override;
        EntryFrame::pointer storeLoad(LedgerKey const& key, Database& db) override;
        EntryFrame::pointer fromXDR(LedgerEntry const& from) override;
        uint64_t countObjects(soci::session& sess) override;

        bool exists(Database& db, uint64_t saleID);

        SaleFrame::pointer loadSale(uint64_t saleID, AccountID ownerID, Database& db, LedgerDelta* delta = nullptr);
        SaleFrame::pointer loadSale(uint64_t saleID, Database& db, LedgerDelta* delta = nullptr);
        SaleFrame::pointer loadSale(uint64_t saleID, AssetCode const& base, AssetCode const& quote, Database& db, LedgerDelta* delta = nullptr);

        std::vector<SaleFrame::pointer> loadSalesForOwner(AccountID owner, Database& db);

    private:
        SaleHelper() { ; }
        ~SaleHelper() { ; }

        void storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert, LedgerEntry const& entry);
        void loadSales(Database& db, StatementContext & prep, std::function<void(LedgerEntry const&)> requestsProcessor) const;
    };
}