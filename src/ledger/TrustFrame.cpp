// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "TrustFrame.h"
#include "crypto/SecretKey.h"
#include "crypto/Hex.h"
#include "database/Database.h"
#include "LedgerDelta.h"
#include "util/basen.h"
#include "util/types.h"
#include "lib/util/format.h"
#include <algorithm>

using namespace soci;
using namespace std;

namespace stellar {
    using xdr::operator<;

    TrustFrame::TrustFrame()
            : EntryFrame(LedgerEntryType::TRUST), mTrustEntry(mEntry.data.trust()) {
    }

    TrustFrame::TrustFrame(LedgerEntry const &from)
            : EntryFrame(from), mTrustEntry(mEntry.data.trust()) {
    }

    TrustFrame::TrustFrame(TrustFrame const &from) : TrustFrame(from.mEntry) {
    }

    bool
    TrustFrame::isValid() {
        return true;
    }

    TrustFrame::pointer TrustFrame::createNew(AccountID allowedAccount, BalanceID balanceToUse) {
        LedgerEntry le;
        le.data.type(LedgerEntryType::TRUST);
        TrustEntry &entry = le.data.trust();

        entry.allowedAccount = allowedAccount;
        entry.balanceToUse = balanceToUse;
        return std::make_shared<TrustFrame>(le);
    }

    AccountID const &TrustFrame::getAllowedAccount() const {
        return mTrustEntry.allowedAccount;
    }

    BalanceID const &TrustFrame::getBalanceToUse() const {
        return mTrustEntry.balanceToUse;
    }

    TrustEntry const &TrustFrame::getTrust() const {
        return mTrustEntry;
    }

    TrustEntry &TrustFrame::getTrust() {
        clearCached();
        return mTrustEntry;
    }

    EntryFrame::pointer TrustFrame::copy() const {
        return EntryFrame::pointer(new TrustFrame(*this));
    }

}
