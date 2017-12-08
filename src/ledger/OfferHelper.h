#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryHelper.h"
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

    class OfferHelper : public EntryHelper {
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

        void loadOffersWithPriceLower(AssetCode const& base, AssetCode const& quote,
                                      int64_t price, std::vector<OfferFrame::pointer>& retOffers, Database& db);

        std::unordered_map<AccountID, std::vector<OfferFrame::pointer>> loadAllOffers(Database& db);

        void loadBestOffers(size_t numOffers, size_t offset,
                            AssetCode const& base, AssetCode const& quote,
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