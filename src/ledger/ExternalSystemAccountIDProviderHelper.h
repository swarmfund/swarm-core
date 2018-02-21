#pragma once

#include "ledger/EntryHelper.h"
#include "ledger/ExternalSystemAccountIDProvider.h"

namespace soci
{
    class session;
}

namespace stellar
{
    class StatementContext;

    class ExternalSystemAccountIDProviderHelper : public EntryHelper {
    public:
        ExternalSystemAccountIDProviderHelper(ExternalSystemAccountIDProviderHelper const&) = delete;
        ExternalSystemAccountIDProviderHelper& operator=(ExternalSystemAccountIDProviderHelper const&) = delete;

        static ExternalSystemAccountIDProviderHelper * Instance() {
            static ExternalSystemAccountIDProviderHelper singleton;
            return &singleton;
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

        bool exists(Database& db, uint64_t providerID);

        ExternalSystemAccountIDProviderFrame::pointer load(uint64_t providerID, Database& db,
                                                           LedgerDelta* delta = nullptr);

        ExternalSystemAccountIDProviderFrame::pointer load(ExternalSystemType type, std::string data,
                                                           Database& db, LedgerDelta* delta = nullptr);

        std::vector<ExternalSystemAccountIDProviderFrame::pointer> loadPool(Database& db);

    private:
        ExternalSystemAccountIDProviderHelper() { ; }
        ~ExternalSystemAccountIDProviderHelper() { ; }

        static const char* select;

        void storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert, LedgerEntry const& entry);
        void load(StatementContext& prep, std::function<void(LedgerEntry const&)> processor);
    };
}
