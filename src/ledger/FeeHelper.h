#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryHelperLegacy.h"
#include "ledger/LedgerManager.h"
#include <functional>
#include <unordered_map>
#include "FeeFrame.h"

namespace soci {
    class session;
}

namespace stellar {
    class StatementContext;

    class FeeHelper : public EntryHelperLegacy {
    public:

        FeeHelper(FeeHelper const &) = delete;

        FeeHelper &operator=(FeeHelper const &) = delete;

        static FeeHelper *Instance() {
            static FeeHelper singleton;
            return &singleton;
        }

        void addFeeAsset(Database &db);

        void dropAll(Database &db) override;

        void storeAdd(LedgerDelta &delta, Database &db, LedgerEntry const &entry) override;

        void storeChange(LedgerDelta &delta, Database &db, LedgerEntry const &entry) override;

        void storeDelete(LedgerDelta &delta, Database &db, LedgerKey const &key) override;

        bool exists(Database &db, LedgerKey const &key) override;

        bool exists(Database &db, Hash hash, int64_t lowerBound, int64_t upperBound);

        bool isBoundariesOverlap(Hash hash, int64_t lowerBound, int64_t upperBound, Database &db);

        uint64_t countObjects(soci::session &sess) override;

        LedgerKey getLedgerKey(LedgerEntry const &from) override;

        EntryFrame::pointer fromXDR(LedgerEntry const &from) override;

        EntryFrame::pointer storeLoad(LedgerKey const &key, Database &db) override;

        FeeFrame::pointer loadFee(FeeType feeType, AssetCode asset, AccountID *accountID,
                                  AccountType *accountType, int64_t subtype, int64_t lowerBound,
                                  int64_t upperBound, Database &db, LedgerDelta *delta = nullptr);

        FeeFrame::pointer loadFee(Hash hash, int64_t lowerBound, int64_t upperBound,
                                  Database &db, LedgerDelta *delta = nullptr);

        FeeFrame::pointer loadForAccount(FeeType feeType, AssetCode asset, int64_t subtype,
                                         AccountFrame::pointer accountFrame, int64_t amount,
                                         Database &db, LedgerDelta *delta = nullptr);

        std::vector<FeeFrame::pointer> loadFees(Hash hash, Database &db);

    private:
        FeeHelper() { ; }

        ~FeeHelper() { ; }

        void storeUpdateHelper(LedgerDelta &delta, Database &db, bool insert, LedgerEntry const &entry);

        void loadFees(StatementContext &prep, std::function<void(LedgerEntry const &)> feeProcessor);
    };
}