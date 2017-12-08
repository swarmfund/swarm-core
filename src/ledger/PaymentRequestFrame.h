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

    PaymentRequestEntry& mPaymentRequest;

    PaymentRequestFrame(PaymentRequestFrame const& from);

  public:
    typedef std::shared_ptr<PaymentRequestFrame> pointer;

    PaymentRequestFrame();
    PaymentRequestFrame(LedgerEntry const& from);

    PaymentRequestFrame& operator=(PaymentRequestFrame const& other);

    void setInvoiceID(uint64 invoiceID);

    bool isValid() const;

    BalanceID getSourceBalance();

    int64 getSourceSend();
    int64 getPaymentID();
    int64 getSourceSendUniversal();
    int64 getDestinationReceive();

    uint64* getInvoiceID();

    uint64 getCreatedAt();

    xdr::pointer<BalanceID> getDestinationBalance();

    EntryFrame::pointer copy() const override;

    PaymentRequestEntry& getPaymentRequest();

    PaymentRequestEntry const& getPaymentRequest() const;

    static bool isValid(PaymentRequestEntry const& oe);
};
}
