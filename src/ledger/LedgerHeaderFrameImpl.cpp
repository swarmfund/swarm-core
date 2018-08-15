// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "LedgerHeaderFrameImpl.h"
#include "LedgerManager.h"
#include "crypto/Hex.h"
#include "crypto/SHA.h"
#include "database/Database.h"
#include "util/Logging.h"
#include "util/XDRStream.h"
#include "util/asio.h"
#include "util/format.h"
#include "util/types.h"
#include "xdrpp/marshal.h"
#include <util/basen.h>

namespace stellar
{

using namespace soci;
using namespace std;

LedgerHeaderFrameImpl::LedgerHeaderFrameImpl(LedgerHeader const& lh)
    : mHeader(lh)
{
    mHash.fill(0);
}

LedgerHeaderFrameImpl::LedgerHeaderFrameImpl(
    LedgerHeaderHistoryEntry const& lastClosed)
    : mHeader(lastClosed.header)
{
    if (getHash() != lastClosed.hash)
    {
        throw std::invalid_argument("provided ledger header is invalid");
    }
    mHeader.ledgerSeq++;
    mHeader.previousLedgerHash = lastClosed.hash;
    mHash.fill(0);
}

Hash const&
LedgerHeaderFrameImpl::getHash() const
{
    if (isZero(mHash))
    {
        mHash = sha256(xdr::xdr_to_opaque(mHeader));
        assert(!isZero(mHash));
    }
    return mHash;
}

const LedgerHeader&
LedgerHeaderFrameImpl::getHeader() const
{
    return mHeader;
}

LedgerHeader&
LedgerHeaderFrameImpl::getHeader()
{
    return mHeader;
}

IdGenerator& LedgerHeaderFrameImpl::getIDGenerator(const LedgerEntryType entryType)
{
    for (auto& generator : mHeader.idGenerators)
    {
        if (generator.entryType == entryType)
        {
            return generator;
        }
    }

    const auto generator = IdGenerator(entryType, 0);
    mHeader.idGenerators.push_back(generator);
    return mHeader.idGenerators[mHeader.idGenerators.size() - 1];
}

uint64_t
LedgerHeaderFrameImpl::getLastGeneratedID(
    const LedgerEntryType ledgerEntryType) const
{
    for (auto& generator : mHeader.idGenerators)
    {
        if (generator.entryType == ledgerEntryType)
        {
            return generator.idPool;
        }
    }

    return 0;
}

uint64_t
LedgerHeaderFrameImpl::generateID(const LedgerEntryType ledgerEntryType)
{
    return ++getIDGenerator(ledgerEntryType).idPool;
}

void
LedgerHeaderFrameImpl::storeInsert(LedgerManager& ledgerManager) const
{
    getHash();

    string hash(binToHex(mHash)),
        prevHash(binToHex(mHeader.previousLedgerHash)),
        bucketListHash(binToHex(mHeader.bucketListHash));

    auto headerBytes(xdr::xdr_to_opaque(mHeader));

    std::string headerEncoded;
    headerEncoded = bn::encode_b64(headerBytes);
    int32_t headerVersion = static_cast<int32_t>(mHeader.ext.v());

    auto& db = ledgerManager.getDatabase();

    // note: columns other than "data" are there to faciliate lookup/processing
    auto prep =
        db.getPreparedStatement("INSERT INTO ledgerheaders "
                                "(ledgerhash, prevhash, bucketlisthash, "
                                "ledgerseq, closetime, data, version) "
                                "VALUES "
                                "(:h,        :ph,      :blh,            :seq,  "
                                "   :ct,       :data, :v)");
    auto& st = prep.statement();
    st.exchange(use(hash));
    st.exchange(use(prevHash));
    st.exchange(use(bucketListHash));
    st.exchange(use(mHeader.ledgerSeq));
    st.exchange(use(mHeader.scpValue.closeTime));
    st.exchange(use(headerEncoded));
    st.exchange(use(headerVersion));
    st.define_and_bind();
    {
        auto timer = db.getInsertTimer("ledger-header");
        st.execute(true);
    }
    if (st.get_affected_rows() != 1)
    {
        throw std::runtime_error("Could not update data in SQL");
    }
}

} // namespace stellar
