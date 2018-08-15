// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/asio.h"
#include "LedgerHeaderFrame.h"
#include "LedgerHeaderFrameImpl.h"
#include "LedgerManager.h"
#include "util/XDRStream.h"
#include "util/Logging.h"
#include "crypto/Hex.h"
#include "crypto/SHA.h"
#include "xdrpp/marshal.h"
#include "database/Database.h"
#include "util/types.h"
#include <util/basen.h>
#include "util/format.h"

namespace stellar
{

using namespace soci;
using namespace std;

LedgerHeaderFrame::pointer
LedgerHeaderFrame::decodeFromData(std::string const& data)
{
    LedgerHeader lh;
    std::vector<uint8_t> decoded;
    bn::decode_b64(data, decoded);

    xdr::xdr_get g(&decoded.front(), &decoded.back() + 1);
    xdr::xdr_argpack_archive(g, lh);
    g.done();

    return make_shared<LedgerHeaderFrameImpl>(lh);
}

LedgerHeaderFrame::pointer
LedgerHeaderFrame::loadByHash(Hash const& hash, Database& db)
{
    LedgerHeaderFrame::pointer lhf;

    string hash_s(binToHex(hash));
    string headerEncoded;

    auto prep = db.getPreparedStatement("SELECT data FROM ledgerheaders "
                                        "WHERE ledgerhash = :h");
    auto& st = prep.statement();
    st.exchange(into(headerEncoded));
    st.exchange(use(hash_s));
    st.define_and_bind();
    {
        auto timer = db.getSelectTimer("ledger-header");
        st.execute(true);
    }
    if (st.got_data())
    {
        lhf = decodeFromData(headerEncoded);
        if (lhf->getHash() != hash)
        {
            // wrong hash
            lhf.reset();
        }
    }

    return lhf;
}

LedgerHeaderFrame::pointer
LedgerHeaderFrame::loadBySequence(uint32_t seq, Database& db,
                                  soci::session& sess)
{
    LedgerHeaderFrame::pointer lhf;

    string headerEncoded;
    {
        auto timer = db.getSelectTimer("ledger-header");
        sess << "SELECT data FROM ledgerheaders "
                "WHERE ledgerseq = :s",
            into(headerEncoded), use(seq);
    }
    if (sess.got_data())
    {
        lhf = decodeFromData(headerEncoded);
        uint32_t loadedSeq = lhf->getHeader().ledgerSeq;

        if (loadedSeq != seq)
        {
            throw std::runtime_error(
                fmt::format("Wrong sequence number in ledger header database: "
                            "loaded ledger {} contains {}",
                            seq, loadedSeq));
        }
    }

    return lhf;
}

size_t
LedgerHeaderFrame::copyLedgerHeadersToStream(Database& db, soci::session& sess,
                                             uint32_t ledgerSeq,
                                             uint32_t ledgerCount,
                                             XDROutputFileStream& headersOut)
{
    auto timer = db.getSelectTimer("ledger-header-history");
    uint32_t begin = ledgerSeq, end = ledgerSeq + ledgerCount;
    size_t n = 0;

    string headerEncoded;

    assert(begin <= end);

    soci::statement st =
        (sess.prepare << "SELECT data FROM ledgerheaders "
                         "WHERE ledgerseq >= :begin AND ledgerseq < :end ORDER "
                         "BY ledgerseq ASC",
         into(headerEncoded), use(begin), use(end));

    st.execute(true);
    while (st.got_data())
    {
        LedgerHeaderHistoryEntry lhe;
        LedgerHeaderFrame::pointer lhf = decodeFromData(headerEncoded);
        lhe.hash = lhf->getHash();
        lhe.header = lhf->getHeader();
        CLOG(DEBUG, "Ledger") << "Streaming ledger-header "
                              << lhe.header.ledgerSeq;
        headersOut.writeOne(lhe);
        ++n;
        st.fetch();
    }
    return n;
}

void
LedgerHeaderFrame::deleteOldEntries(Database& db, uint32_t ledgerSeq)
{
    db.getSession() << "DELETE FROM ledgerheaders WHERE ledgerseq <= "
                    << ledgerSeq;
}

void
LedgerHeaderFrame::dropAll(Database& db)
{
    db.getSession() << "DROP TABLE IF EXISTS ledgerheaders;";

    db.getSession() << "CREATE TABLE ledgerheaders ("
                       "ledgerhash      CHARACTER(64) PRIMARY KEY,"
                       "prevhash        CHARACTER(64) NOT NULL,"
                       "bucketlisthash  CHARACTER(64) NOT NULL,"
                       "ledgerseq       INT           UNIQUE    CHECK (ledgerseq >= 0),"
                       "closetime       BIGINT        NOT NULL  CHECK (closetime >= 0),"
                       "data            TEXT          NOT NULL,"
                       "version         INT           NOT NULL  DEFAULT 0"
                       ");";

    db.getSession()
        << "CREATE INDEX ledgersbyseq ON ledgerheaders ( ledgerseq );";
}
}
