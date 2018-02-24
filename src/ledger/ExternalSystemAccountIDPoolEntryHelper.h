#pragma once

#include "ledger/EntryHelper.h"
#include "ledger/ExternalSystemAccountIDPoolEntry.h"

namespace soci
{
    class session;
}

namespace stellar
{
    class StatementContext;

    class ExternalSystemAccountIDPoolEntryHelper : public EntryHelper {
    public:
        ExternalSystemAccountIDPoolEntryHelper(ExternalSystemAccountIDPoolEntryHelper const&) = delete;
        ExternalSystemAccountIDPoolEntryHelper& operator=(ExternalSystemAccountIDPoolEntryHelper const&) = delete;

        static ExternalSystemAccountIDPoolEntryHelper * Instance() {
            static ExternalSystemAccountIDPoolEntryHelper singleton;
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

        bool exists(Database& db, uint64_t poolEntryID);
        bool existsForAccount(Database& db, ExternalSystemType externalSystemType, AccountID accountID);

        ExternalSystemAccountIDPoolEntryFrame::pointer load(uint64_t poolEntryID, Database& db,
                                                           LedgerDelta* delta = nullptr);

        ExternalSystemAccountIDPoolEntryFrame::pointer load(ExternalSystemType type, std::string data,
                                                           Database& db, LedgerDelta* delta = nullptr);
        ExternalSystemAccountIDPoolEntryFrame::pointer load(ExternalSystemType externalSystemType, AccountID accountID,
                                                            Database& db, LedgerDelta* delta = nullptr);

        ExternalSystemAccountIDPoolEntryFrame::pointer loadAvailablePoolEntry(Database &db,
                                                                              LedgerManager &ledgerManager,
                                                                              ExternalSystemType externalSystemType);

        std::vector<ExternalSystemAccountIDPoolEntryFrame::pointer> loadPool(Database& db);

    private:
        ExternalSystemAccountIDPoolEntryHelper() { ; }
        ~ExternalSystemAccountIDPoolEntryHelper() { ; }

        static const char* select;

        void storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert, LedgerEntry const& entry);
        void load(StatementContext& prep, std::function<void(LedgerEntry const&)> processor);
    };
}
