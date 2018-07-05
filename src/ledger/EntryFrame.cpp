// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryFrame.h"
#include "LedgerManager.h"
#include "ledger/AccountFrame.h"
#include "ledger/ReferenceFrame.h"
#include "ledger/StatisticsFrame.h"
#include "ledger/AccountTypeLimitsFrame.h"
#include "ledger/AccountLimitsFrame.h"
#include "ledger/AssetFrame.h"
#include "ledger/AssetPairFrame.h"
#include "ledger/BalanceFrame.h"
#include "ledger/LedgerDelta.h"
#include "ledger/FeeFrame.h"
#include "ledger/PaymentRequestFrame.h"
#include "ledger/TrustFrame.h"
#include "ledger/OfferFrame.h"
#include "ledger/ReviewableRequestFrame.h"
#include "ledger/ExternalSystemAccountID.h"
#include "xdrpp/printer.h"
#include "xdrpp/marshal.h"
#include "crypto/Hex.h"
#include "database/Database.h"

namespace stellar {
    using xdr::operator==;

	EntryFrame::EntryFrame(LedgerEntryType type) : mKeyCalculated(false) {
		mEntry.data.type(type);
	}

	EntryFrame::EntryFrame(LedgerEntry const &from)
		: mKeyCalculated(false), mEntry(from) {
	}

	LedgerKey const &
		EntryFrame::getKey() const {
		if (!mKeyCalculated) {
			mKey = LedgerEntryKey(mEntry);
			mKeyCalculated = true;
		}
		return mKey;
	}

    uint32
    EntryFrame::getLastModified() const {
        return mEntry.lastModifiedLedgerSeq;
    }

    uint32 &
    EntryFrame::getLastModified() {
        return mEntry.lastModifiedLedgerSeq;
    }

    void
    EntryFrame::touch(uint32 ledgerSeq) {
        assert(ledgerSeq != 0);
        getLastModified() = ledgerSeq;
    }

    void
    EntryFrame::touch(LedgerDelta const &delta) {
        uint32 ledgerSeq = delta.getHeader().ledgerSeq;
        if (delta.updateLastModified()) {
            touch(ledgerSeq);
        }
    }

}
