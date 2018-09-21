#pragma once

#include "EntryHelperLegacy.h"
#include "SaleAnteFrame.h"

namespace soci {
    class session;
}

namespace stellar {
    class StatementContext;

    class SaleAnteHelper : public EntryHelperLegacy {
        SaleAnteHelper() { ; }

        ~SaleAnteHelper() { ; }

        void storeUpdateHelper(LedgerDelta &delta, Database &db, bool insert, LedgerEntry const &entry);

        void loadSaleAntes(Database &db, StatementContext &prep,
                           std::function<void(LedgerEntry const &)> saleAnteProcessor) const;

    public:
        SaleAnteHelper(SaleAnteHelper const &) = delete;

        SaleAnteHelper &operator=(SaleAnteHelper const &) = delete;

        static SaleAnteHelper *Instance() {
            static SaleAnteHelper singleton;
            return &singleton;
        }

        void dropAll(Database &db) override;

        void storeAdd(LedgerDelta &delta, Database &db, LedgerEntry const &entry) override;

        void storeChange(LedgerDelta &delta, Database &db, LedgerEntry const &entry) override;

        void storeDelete(LedgerDelta &delta, Database &db, LedgerKey const &key) override;

        bool exists(Database &db, LedgerKey const &key) override;

        LedgerKey getLedgerKey(LedgerEntry const &from) override;

        EntryFrame::pointer storeLoad(LedgerKey const &key, Database &db) override;

        EntryFrame::pointer fromXDR(LedgerEntry const &from) override;

        uint64_t countObjects(soci::session &sess) override;

        SaleAnteFrame::pointer loadSaleAnte(uint64 saleID, BalanceID const &participantBalanceID,
                                            Database &db, LedgerDelta *delta = nullptr);

        std::vector<SaleAnteFrame::pointer> loadSaleAntesForSale(uint64_t saleID, Database &db);

        std::unordered_map<BalanceID, SaleAnteFrame::pointer> loadSaleAntes(uint64_t saleID, Database &db);
    };
}