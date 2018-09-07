#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryHelperLegacy.h"
#include "ledger/LedgerManager.h"
#include <functional>
#include <unordered_map>
#include "OfferFrame.h"

namespace soci
{
    class session;
}

namespace stellar
{
    class StatementContext;

    class OfferHelper : public EntryHelperLegacy {

    public:
        OfferHelper(OfferHelper const&) = delete;
        OfferHelper& operator= (OfferHelper const&) = delete;

        static OfferHelper *Instance() {
            static OfferHelper singleton;
            return&singleton;
        }

        void dropAll(Database& db) override;
        void storeAdd(LedgerDelta& delta, Database& db, LedgerEntry const& entry) override;
        void storeChange(LedgerDelta& delta, Database& db, LedgerEntry const& entry) override;
        void storeDelete(LedgerDelta& delta, Database& db, LedgerKey const& key) override;
        bool exists(Database& db, LedgerKey const& key) override;
        LedgerKey getLedgerKey(LedgerEntry const& from) override;
        EntryFrame::pointer storeLoad(LedgerKey const& key, Database& db) override;
        EntryFrame::pointer fromXDR(LedgerEntry const& from) override;
        uint64_t countObjects(soci::session& sess) override;

        OfferFrame::pointer loadOffer(AccountID const& accountID, uint64_t offerID,
                          Database& db, LedgerDelta* delta = nullptr);

        OfferFrame::pointer loadOffer(AccountID const& accountID, uint64_t offerID, uint64_t orderBookID,
            Database& db, LedgerDelta* delta = nullptr);

        std::vector<OfferFrame::pointer> loadOffersWithFilters(AssetCode const& base, AssetCode const& quote, uint64_t* orderBookID,
            uint64_t* priceUpperBound, Database& db);

        std::vector<OfferFrame::pointer> loadOffers(AssetCode const& base, AssetCode const& quote, uint64_t const orderBookID,
            int64_t quoteamountUpperBound, Database& db);

        std::unordered_map<AccountID, std::vector<OfferFrame::pointer>> loadAllOffers(Database& db);

        void loadBestOffers(size_t numOffers, size_t offset,
                            AssetCode const& base, AssetCode const& quote, uint64_t orderBookID,
                            bool isBuy,
                            std::vector<OfferFrame::pointer>& retOffers,
                            Database& db);
    private:
        OfferHelper() { ; }
        ~OfferHelper() { ; }

        void storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert, LedgerEntry const& entry);

        void loadOffers(StatementContext& prep, std::function<void(LedgerEntry const&)> offerProcessor);


    };
}