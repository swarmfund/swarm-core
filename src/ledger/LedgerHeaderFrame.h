#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "overlay/StellarXDR.h"

namespace soci
{
class session;
}

namespace stellar
{
class LedgerManager;
class Database;
class XDROutputFileStream;

class LedgerHeaderFrame
{
  public:
    typedef std::shared_ptr<LedgerHeaderFrame> pointer;

    virtual ~LedgerHeaderFrame() = default;

    virtual Hash const& getHash() const = 0;

    virtual const LedgerHeader& getHeader() const = 0;
    virtual LedgerHeader& getHeader() = 0;

    // methods to generate IDs
    virtual uint64_t
    getLastGeneratedID(const LedgerEntryType ledgerEntryType) const = 0;
    // generates a new ID and returns it
    virtual uint64_t generateID(const LedgerEntryType ledgerEntryType) = 0;

    virtual void storeInsert(LedgerManager& ledgerManager) const = 0;

    static LedgerHeaderFrame::pointer loadByHash(Hash const& hash,
                                                 Database& db);
    static LedgerHeaderFrame::pointer loadBySequence(uint32_t seq, Database& db,
                                                     soci::session& sess);

    static size_t copyLedgerHeadersToStream(Database& db, soci::session& sess,
                                            uint32_t ledgerSeq,
                                            uint32_t ledgerCount,
                                            XDROutputFileStream& headersOut);

    static void deleteOldEntries(Database& db, uint32_t ledgerSeq);

    static void dropAll(Database& db);
    static const char* kSQLCreateStatement;

  private:
    static LedgerHeaderFrame::pointer decodeFromData(std::string const& data);
};
} // namespace stellar
