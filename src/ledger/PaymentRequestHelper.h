#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryHelper.h"
#include "ledger/LedgerManager.h"
#include <functional>
#include <unordered_map>
#include "PaymentRequestFrame.h"

namespace soci
{
    class session;
}

namespace stellar
{
    class StatementContext;

    class PaymentRequestHelper : public EntryHelper {
    public:
        PaymentRequestHelper(PaymentRequestHelper const&) = delete;
        PaymentRequestHelper& operator= (PaymentRequestHelper const&) = delete;

        static PaymentRequestHelper *Instance() {
            static PaymentRequestHelper singleton;
            return&singleton;
        }

        void dropAll(Database& db) override;
        void storeAdd(LedgerDelta& delta, Database& db, LedgerEntry const& entry) override;
        void storeChange(LedgerDelta& delta, Database& db, LedgerEntry const& entry) override;
        void storeDelete(LedgerDelta& delta, Database& db, LedgerKey const& key) override;
        bool exists(Database& db, LedgerKey const& key) override;
        bool exists(Database& db, int64 paymentID);
        LedgerKey getLedgerKey(LedgerEntry const& from) override;
        EntryFrame::pointer storeLoad(LedgerKey const& key, Database& db) override;
        EntryFrame::pointer fromXDR(LedgerEntry const& from) override;
        uint64_t countObjects(soci::session& sess) override;

        PaymentRequestFrame::pointer loadPaymentRequest(int64 paymentID, Database& db, LedgerDelta* delta = nullptr);

    private:
        PaymentRequestHelper() { ; }
        ~PaymentRequestHelper() { ; }

        void
        loadPaymentRequests(StatementContext& prep,
                            std::function<void(LedgerEntry const&)> PaymentRequestProcessor);

        void storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert, LedgerEntry const& entry);
    };
}