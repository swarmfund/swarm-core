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

class PaymentRequestFrame : public EntryFrame
{
    static void
    loadPaymentRequests(StatementContext& prep,
               std::function<void(LedgerEntry const&)> PaymentRequestProcessor);

    PaymentRequestEntry& mPaymentRequest;

    PaymentRequestFrame(PaymentRequestFrame const& from);

    void storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert);

  public:
    typedef std::shared_ptr<PaymentRequestFrame> pointer;

    PaymentRequestFrame();
    PaymentRequestFrame(LedgerEntry const& from);

    PaymentRequestFrame& operator=(PaymentRequestFrame const& other);

    EntryFrame::pointer
    copy() const override
    {
        return EntryFrame::pointer(new PaymentRequestFrame(*this));
    }

    PaymentRequestEntry const&
    getPaymentRequest() const
    {
        return mPaymentRequest;
    }
    PaymentRequestEntry&
    getPaymentRequest()
    {
        return mPaymentRequest;
    }

    BalanceID
    getSourceBalance()
    {
        return mPaymentRequest.sourceBalance;
    }
    
    int64
    getSourceSend()
    {
        return mPaymentRequest.sourceSend;
    }

    int64
    getSourceSendUniversal()
    {
        return mPaymentRequest.sourceSendUniversal;
    }


    int64
    getDestinationReceive()
    {
        return mPaymentRequest.destinationReceive;
    }

    uint64
    getCreatedAt()
    {
        return mPaymentRequest.createdAt;
    }


    void
    setInvoiceID(uint64 invoiceID)
    {
        mPaymentRequest.invoiceID.activate() = invoiceID;
    }


    uint64*
    getInvoiceID()
    {
        return mPaymentRequest.invoiceID.get();
    }


    static bool isValid(PaymentRequestEntry const& oe);
    bool isValid() const;

    // Instance-based overrides of EntryFrame.
    void storeDelete(LedgerDelta& delta, Database& db) const override;
    void storeChange(LedgerDelta& delta, Database& db) override;
    void storeAdd(LedgerDelta& delta, Database& db) override;

    // Static helpers that don't assume an instance.
    static void storeDelete(LedgerDelta& delta, Database& db,
                            LedgerKey const& key);
	static bool exists(Database& db, LedgerKey const& key);
	static bool exists(Database& db, int64 paymentID);
    static uint64_t countObjects(soci::session& sess);

    // database utilities
    static pointer loadPaymentRequest(int64 paymentID, Database& db, LedgerDelta* delta = nullptr);


    static void dropAll(Database& db);
    static const char* kSQLCreateStatement1;
};
}
