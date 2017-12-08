// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/PaymentRequestFrame.h"
#include "database/Database.h"
#include "crypto/SecretKey.h"
#include "crypto/SHA.h"
#include "LedgerDelta.h"
#include "util/types.h"

using namespace std;
using namespace soci;

namespace stellar
{


PaymentRequestFrame::PaymentRequestFrame() : EntryFrame(LedgerEntryType::PAYMENT_REQUEST), mPaymentRequest(mEntry.data.paymentRequest())
{
}

PaymentRequestFrame::PaymentRequestFrame(LedgerEntry const& from)
    : EntryFrame(from), mPaymentRequest(mEntry.data.paymentRequest())
{
}

PaymentRequestFrame::PaymentRequestFrame(PaymentRequestFrame const& from) : PaymentRequestFrame(from.mEntry)
{
}

PaymentRequestFrame& PaymentRequestFrame::operator=(PaymentRequestFrame const& other)
{
    if (&other != this)
    {
        mPaymentRequest = other.mPaymentRequest;
        mKey = other.mKey;
        mKeyCalculated = other.mKeyCalculated;
    }
    return *this;
}

bool
PaymentRequestFrame::isValid(PaymentRequestEntry const& oe)
{
	return oe.sourceSend >= 0 && oe.sourceSendUniversal >= 0 && oe.destinationReceive >= 0;
}

bool
PaymentRequestFrame::isValid() const
{
    return isValid(mPaymentRequest);
}

    PaymentRequestEntry const &PaymentRequestFrame::getPaymentRequest() const {
        return mPaymentRequest;
    }

    PaymentRequestEntry &PaymentRequestFrame::getPaymentRequest() {
        return mPaymentRequest;
    }

    BalanceID PaymentRequestFrame::getSourceBalance() {
        return mPaymentRequest.sourceBalance;
    }

    int64 PaymentRequestFrame::getSourceSend() {
        return mPaymentRequest.sourceSend;
    }

    int64 PaymentRequestFrame::getSourceSendUniversal() {
        return mPaymentRequest.sourceSendUniversal;
    }

    int64 PaymentRequestFrame::getDestinationReceive() {
        return mPaymentRequest.destinationReceive;
    }

    xdr::pointer<BalanceID> PaymentRequestFrame::getDestinationBalance() {
        return mPaymentRequest.destinationBalance;
    }

    uint64 PaymentRequestFrame::getCreatedAt() {
        return mPaymentRequest.createdAt;
    }

    void PaymentRequestFrame::setInvoiceID(uint64 invoiceID) {
        mPaymentRequest.invoiceID.activate() = invoiceID;
    }

    uint64 *PaymentRequestFrame::getInvoiceID() {
        return mPaymentRequest.invoiceID.get();
    }

    int64 PaymentRequestFrame::getPaymentID() {
        return mPaymentRequest.paymentID;
    }

    EntryFrame::pointer PaymentRequestFrame::copy() const {
        return EntryFrame::pointer(new PaymentRequestFrame(*this));
    }

}
