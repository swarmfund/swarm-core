// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "OperationFrame.h"
#include "TransactionFrameImpl.h"
#include "TransactionFrame.h"
#include "crypto/Hex.h"
#include "crypto/SHA.h"
#include "crypto/SecretKey.h"
#include "database/Database.h"
#include "herder/TxSetFrame.h"
#include "ledger/AccountHelper.h"
#include "ledger/LedgerDelta.h"
#include "ledger/StorageHelperImpl.h"
#include "ledger/LedgerHeaderFrame.h"
#include "main/Application.h"
#include "util/Logging.h"
#include "util/XDRStream.h"
#include "util/asio.h"
#include "util/basen.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"

namespace stellar
{

using namespace std;
using xdr::operator==;

TransactionFramePtr
TransactionFrame::makeTransactionFromWire(Hash const& networkID,
                                          TransactionEnvelope const& msg)
{
    TransactionFramePtr res = make_shared<TransactionFrameImpl>(networkID, msg);
    return res;
}

static void
saveTransactionHelper(Database& db, soci::session& sess, uint32 ledgerSeq,
                      TxSetFrame& txSet, TransactionHistoryResultEntry& results,
                      XDROutputFileStream& txOut,
                      XDROutputFileStream& txResultOut)
{
    // prepare the txset for saving
    LedgerHeaderFrame::pointer lh =
        LedgerHeaderFrame::loadBySequence(ledgerSeq, db, sess);
    if (!lh)
    {
        throw std::runtime_error("Could not find ledger");
    }
    txSet.previousLedgerHash() = lh->getHeader().previousLedgerHash;
    txSet.sortForHash();
    TransactionHistoryEntry hist;
    hist.ledgerSeq = ledgerSeq;
    txSet.toXDR(hist.txSet);
    txOut.writeOne(hist);

    txResultOut.writeOne(results);
}

bool
TransactionFrame::timingExists(Database& db, std::string txID)
{
    int exists = 0;
    auto timer = db.getSelectTimer("txtiming-exists");
    auto prep = db.getPreparedStatement(
        "SELECT EXISTS (SELECT NULL FROM txtiming WHERE txid=:id)");
    auto& st = prep.statement();
    st.exchange(soci::use(txID));
    st.exchange(soci::into(exists));
    st.define_and_bind();
    st.execute(true);

    return exists != 0;
}

TransactionResultSet
TransactionFrame::getTransactionHistoryResults(Database& db, uint32 ledgerSeq)
{
    TransactionResultSet res;
    std::string txresult64;
    auto prep =
        db.getPreparedStatement("SELECT txresult FROM txhistory "
                                "WHERE ledgerseq = :lseq ORDER BY txindex ASC");
    auto& st = prep.statement();

    st.exchange(soci::use(ledgerSeq));
    st.exchange(soci::into(txresult64));
    st.define_and_bind();
    st.execute(true);
    while (st.got_data())
    {
        std::vector<uint8_t> result;
        bn::decode_b64(txresult64, result);

        res.results.emplace_back();
        TransactionResultPair& p = res.results.back();

        xdr::xdr_get g(&result.front(), &result.back() + 1);
        xdr_argpack_archive(g, p);

        st.fetch();
    }
    return res;
}

std::vector<LedgerEntryChanges>
TransactionFrame::getTransactionFeeMeta(Database& db, uint32 ledgerSeq)
{
    std::vector<LedgerEntryChanges> res;
    std::string changes64;
    auto prep =
        db.getPreparedStatement("SELECT txchanges FROM txfeehistory "
                                "WHERE ledgerseq = :lseq ORDER BY txindex ASC");
    auto& st = prep.statement();

    st.exchange(soci::into(changes64));
    st.exchange(soci::use(ledgerSeq));
    st.define_and_bind();
    st.execute(true);
    while (st.got_data())
    {
        std::vector<uint8_t> changesRaw;
        bn::decode_b64(changes64, changesRaw);

        xdr::xdr_get g1(&changesRaw.front(), &changesRaw.back() + 1);
        res.emplace_back();
        xdr_argpack_archive(g1, res.back());

        st.fetch();
    }
    return res;
}

size_t
TransactionFrame::copyTransactionsToStream(Hash const& networkID, Database& db,
                                           soci::session& sess,
                                           uint32_t ledgerSeq,
                                           uint32_t ledgerCount,
                                           XDROutputFileStream& txOut,
                                           XDROutputFileStream& txResultOut)
{
    auto timer = db.getSelectTimer("txhistory");
    std::string txBody, txResult, txMeta;
    uint32_t begin = ledgerSeq, end = ledgerSeq + ledgerCount;
    size_t n = 0;

    TransactionEnvelope tx;
    uint32_t curLedgerSeq;

    assert(begin <= end);
    soci::statement st =
        (sess.prepare << "SELECT ledgerseq, txbody, txresult FROM txhistory "
                         "WHERE ledgerseq >= :begin AND ledgerseq < :end ORDER "
                         "BY ledgerseq ASC, txindex ASC",
         soci::into(curLedgerSeq), soci::into(txBody), soci::into(txResult),
         soci::use(begin), soci::use(end));

    Hash h;
    TxSetFrame txSet(h); // we're setting the hash later
    TransactionHistoryResultEntry results;

    st.execute(true);

    uint32_t lastLedgerSeq = curLedgerSeq;
    results.ledgerSeq = curLedgerSeq;

    while (st.got_data())
    {
        if (curLedgerSeq != lastLedgerSeq)
        {
            saveTransactionHelper(db, sess, lastLedgerSeq, txSet, results,
                                  txOut, txResultOut);
            // reset state
            txSet.mTransactions.clear();
            results.ledgerSeq = curLedgerSeq;
            results.txResultSet.results.clear();
            lastLedgerSeq = curLedgerSeq;
        }

        std::vector<uint8_t> body;
        bn::decode_b64(txBody, body);

        std::vector<uint8_t> result;
        bn::decode_b64(txResult, result);

        xdr::xdr_get g1(&body.front(), &body.back() + 1);
        xdr_argpack_archive(g1, tx);

        TransactionFramePtr txFrame =
            make_shared<TransactionFrameImpl>(networkID, tx);
        txSet.add(txFrame);

        xdr::xdr_get g2(&result.front(), &result.back() + 1);
        results.txResultSet.results.emplace_back();

        TransactionResultPair& p = results.txResultSet.results.back();
        xdr_argpack_archive(g2, p);

        if (p.transactionHash != txFrame->getContentsHash())
        {
            throw std::runtime_error("transaction mismatch");
        }

        ++n;
        st.fetch();
    }
    if (n != 0)
    {
        saveTransactionHelper(db, sess, lastLedgerSeq, txSet, results, txOut,
                              txResultOut);
    }
    return n;
}

void
TransactionFrame::dropAll(Database& db)
{
    db.getSession() << "DROP TABLE IF EXISTS txhistory";

    db.getSession() << "DROP TABLE IF EXISTS txfeehistory";

    db.getSession() << "DROP TABLE IF EXISTS txtiming";

    db.getSession() << "CREATE TABLE txhistory ("
                       "txid        CHARACTER(64) NOT NULL,"
                       "ledgerseq   INT NOT NULL CHECK (ledgerseq >= 0),"
                       "txindex     INT NOT NULL,"
                       "txbody      TEXT NOT NULL,"
                       "txresult    TEXT NOT NULL,"
                       "txmeta      TEXT NOT NULL,"
                       "PRIMARY KEY (ledgerseq, txindex)"
                       ")";
    db.getSession() << "CREATE INDEX txhistroy_id_index ON txhistory(txid);";

    db.getSession() << "CREATE INDEX histbyseq ON txhistory (ledgerseq);";

    db.getSession() << "CREATE TABLE txfeehistory ("
                       "txid        CHARACTER(64) NOT NULL,"
                       "ledgerseq   INT NOT NULL CHECK (ledgerseq >= 0),"
                       "txindex     INT NOT NULL,"
                       "txchanges   TEXT NOT NULL,"
                       "PRIMARY KEY (ledgerseq, txindex)"
                       ")";

    db.getSession()
        << "CREATE TABLE txtiming ("
           "txid        CHARACTER(64) NOT NULL,"
           "valid_before   BIGINT NOT NULL CHECK (valid_before >= 0),"
           "PRIMARY KEY (txid)"
           ")";

    db.getSession() << "CREATE INDEX histfeebyseq ON txfeehistory (ledgerseq);";
}

void
TransactionFrame::deleteOldEntries(Database& db, uint32_t ledgerSeq,
                                   uint64 ledgerCloseTime)
{
    db.getSession() << "DELETE FROM txhistory WHERE ledgerseq <= " << ledgerSeq;
    db.getSession() << "DELETE FROM txfeehistory WHERE ledgerseq <= "
                    << ledgerSeq;
    db.getSession() << "DELETE FROM txtiming WHERE valid_before < "
                    << ledgerCloseTime;
}
}
