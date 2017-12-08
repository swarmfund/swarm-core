#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryFrame.h"
#include <functional>
#include "map"
#include <unordered_map>
#include "xdr/Stellar-ledger-entries-account.h"

namespace soci
{
class session;
namespace details
{
class prepare_temp_type;
}
}

namespace stellar
{
class LedgerManager;

class TrustFrame : public EntryFrame
{

    TrustEntry& mTrustEntry;

    TrustFrame(TrustFrame const& from);

  public:
    typedef std::shared_ptr<TrustFrame> pointer;

    TrustFrame();
    TrustFrame(LedgerEntry const& from);

    bool isValid();

    EntryFrame::pointer copy() const override;

    AccountID const& getAllowedAccount() const;

    BalanceID const& getBalanceToUse() const;

    TrustEntry const& getTrust() const;

    TrustEntry& getTrust();

	static pointer createNew(AccountID id, BalanceID balanceToUse);
};
}
