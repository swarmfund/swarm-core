#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/LedgerHeaderFrame.h"

namespace stellar
{

class LedgerHeaderFrameImpl : public LedgerHeaderFrame
{
    mutable Hash mHash;

    LedgerHeader mHeader;

    IdGenerator& getIDGenerator(const LedgerEntryType entryType);
	
  public:

    // wraps the given ledger as is
    explicit LedgerHeaderFrameImpl(LedgerHeader const& lh);

    // creates a new, _subsequent_ ledger, following the provided closed ledger
    explicit LedgerHeaderFrameImpl(LedgerHeaderHistoryEntry const& lastClosed);

    Hash const& getHash() const override;

    const LedgerHeader& getHeader() const override;
    LedgerHeader& getHeader() override;

	  // methods to generate IDs
    uint64_t getLastGeneratedID(const LedgerEntryType ledgerEntryType) const override;
    // generates a new ID and returns it
    uint64_t generateID(const LedgerEntryType ledgerEntryType) override;

    void storeInsert(LedgerManager& ledgerManager) const override;

  private:
    static LedgerHeaderFrame::pointer decodeFromData(std::string const& data);
};
}
