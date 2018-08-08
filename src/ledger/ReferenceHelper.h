#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryHelperLegacy.h"
#include "ledger/LedgerManager.h"
#include <functional>
#include <unordered_map>
#include "ReferenceFrame.h"

namespace soci {
    class session;
}

namespace stellar {
    class StatementContext;

    class ReferenceHelper : public EntryHelperLegacy {
    public:
        ReferenceHelper(ReferenceHelper const &) = delete;

        ReferenceHelper &operator=(ReferenceHelper const &) = delete;

        static ReferenceHelper *Instance() {
            static ReferenceHelper singleton;
            return &singleton;
        }

        void dropAll(Database &db) override;
        void storeAdd(LedgerDelta &delta, Database &db, LedgerEntry const &entry) override;
        void storeChange(LedgerDelta &delta, Database &db, LedgerEntry const &entry) override;
        void storeDelete(LedgerDelta &delta, Database &db, LedgerKey const &key) override;

        bool exists(Database &db, LedgerKey const &key) override;
        bool exists(Database &db, std::string reference, AccountID exchange);

        uint64_t countObjects(soci::session &sess) override;

        LedgerKey getLedgerKey(LedgerEntry const &from) override;

        EntryFrame::pointer storeLoad(LedgerKey const &key, Database &db) override;
        EntryFrame::pointer fromXDR(LedgerEntry const &from) override;

        ReferenceFrame::pointer loadReference(AccountID exchange, std::string reference,
                                              Database &db, LedgerDelta *delta = nullptr);

        static void addVersion(Database& db);

    private:
        ReferenceHelper() { ; }
        ~ReferenceHelper() { ; }

        void storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert, const LedgerEntry& entry);
        void loadReferences(StatementContext &prep, std::function<void(LedgerEntry const &)> referenceProcessor);
    };
}