// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/asio.h"
#include "TransactionFrame.h"
#include "OperationFrame.h"
#include "main/Application.h"
#include "xdrpp/marshal.h"
#include <string>
#include "util/Logging.h"
#include "util/XDRStream.h"
#include "ledger/LedgerDelta.h"
#include "crypto/SHA.h"
#include "crypto/SecretKey.h"
#include "database/Database.h"
#include "herder/TxSetFrame.h"
#include "crypto/Hex.h"
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
    TransactionFramePtr res = make_shared<TransactionFrame>(networkID, msg);
    return res;
}

TransactionFrame::TransactionFrame(Hash const& networkID,
                                   TransactionEnvelope const& envelope)
    : mEnvelope(envelope), mNetworkID(networkID)
{
}

Hash const&
TransactionFrame::getFullHash() const
{
    if (isZero(mFullHash))
    {
        mFullHash = sha256(xdr::xdr_to_opaque(mEnvelope));
    }
    return (mFullHash);
}

Hash const&
TransactionFrame::getContentsHash() const
{
    if (isZero(mContentsHash))
    {
        mContentsHash = sha256(
            xdr::xdr_to_opaque(mNetworkID, EnvelopeType::TX, mEnvelope.tx));
    }
    return (mContentsHash);
}

void
TransactionFrame::clearCached()
{
	mSignatureValidator = nullptr;
    Hash zero;
    mContentsHash = zero;
    mFullHash = zero;
}

TransactionResultPair
TransactionFrame::getResultPair() const
{
    TransactionResultPair trp;
    trp.transactionHash = getContentsHash();
    trp.result = mResult;
    return trp;
}

TransactionEnvelope const&
TransactionFrame::getEnvelope() const
{
    return mEnvelope;
}

TransactionEnvelope&
TransactionFrame::getEnvelope()
{
    return mEnvelope;
}

int64_t
TransactionFrame::getPaidFee() const
{
    int64_t fee = 0;
    for (auto op : mOperations)
	{
		// tx is malformed in some way, return 0 to lower it's piority
		if (fee + op->getPaidFee() < 0)
			return 0;
		fee += op->getPaidFee();
    }
    return fee;
}

void
TransactionFrame::addSignature(SecretKey const& secretKey)
{
    clearCached();
    DecoratedSignature sig;
    sig.signature = secretKey.sign(getContentsHash());
    sig.hint = PubKeyUtils::getHint(secretKey.getPublicKey());
    mEnvelope.signatures.push_back(sig);
}

AccountFrame::pointer
TransactionFrame::loadAccount(LedgerDelta* delta, Database& db,
                              AccountID const& accountID)
{
    AccountFrame::pointer res;

    if (mSigningAccount && mSigningAccount->getID() == accountID)
    {
        res = mSigningAccount;
    }
    else if (delta)
    {
        res = AccountFrame::loadAccount(*delta, accountID, db);
    }
    else
    {
        res = AccountFrame::loadAccount(accountID, db);
    }
    return res;
}

bool
TransactionFrame::loadAccount(LedgerDelta* delta, Database& db)
{
    mSigningAccount = loadAccount(delta, db, getSourceID());
    return !!mSigningAccount;
}

void
TransactionFrame::resetResults()
{
    // pre-allocates the results for all operations
    getResult().result.code(TransactionResultCode::txSUCCESS);
    getResult().result.results().resize(
        (uint32_t)mEnvelope.tx.operations.size());

    mOperations.clear();

    // bind operations to the results
    for (size_t i = 0; i < mEnvelope.tx.operations.size(); i++)
    {
        mOperations.push_back(
            OperationFrame::makeHelper(mEnvelope.tx.operations[i],
                                       getResult().result.results()[i], *this));
    }
    // feeCharged is updated accordingly to represent the cost of the
    // transaction regardless of the failure modes.
    getResult().feeCharged = getPaidFee();
}

bool TransactionFrame::doCheckSignature(Application& app, Database& db, AccountFrame& account)
{
	auto signatureValidator = getSignatureValidator();
	// block reasons are handeled on operation level
	auto sourceDetails = SourceDetails(getAllAccountTypes(), mSigningAccount->getLowThreshold(),
                                       getAnySignerType() ^ static_cast<int32_t>(SignerType::READER), getAnyBlockReason());
	SignatureValidator::Result result = signatureValidator->check(app, db, account, sourceDetails);
	switch (result)
	{
	case SignatureValidator::Result::SUCCESS:
		return true;
	case SignatureValidator::Result::INVALID_ACCOUNT_TYPE:
		throw runtime_error("Did not expect INVALID_ACCOUNT_TYPE error for tx");
	case SignatureValidator::Result::NOT_ENOUGH_WEIGHT:
		app.getMetrics().NewMeter({ "transaction", "invalid", "bad-auth" }, "transaction").Mark();
		getResult().result.code(TransactionResultCode::txBAD_AUTH);
		return false;
	case SignatureValidator::Result::EXTRA:
	case SignatureValidator::Result::INVALID_SIGNER_TYPE:
		app.getMetrics().NewMeter({ "transaction", "invalid", "bad-auth-extra" }, "transaction").Mark();
		getResult().result.code(TransactionResultCode::txBAD_AUTH_EXTRA);
		return false;
	case SignatureValidator::Result::ACCOUNT_BLOCKED:
		app.getMetrics().NewMeter({ "transaction", "invalid", "account-blocked" }, "transaction").Mark();
		getResult().result.code(TransactionResultCode::txACCOUNT_BLOCKED);
		return false;
	}

	throw runtime_error("Unexpected error code from signatureValidator");
}

bool
TransactionFrame::commonValid(Application& app, LedgerDelta* delta)
{
    if (mOperations.size() == 0)
    {
        app.getMetrics()
            .NewMeter({"transaction", "invalid", "missing-operation"},
                      "transaction")
            .Mark();
        getResult().result.code(TransactionResultCode::txMISSING_OPERATION);
        return false;
    }
    
    auto& lm = app.getLedgerManager();

    uint64 closeTime = lm.getCurrentLedgerHeader().scpValue.closeTime;

    if (mEnvelope.tx.timeBounds.minTime > closeTime)
    {
        app.getMetrics()
            .NewMeter({"transaction", "invalid", "too-early"},
                        "transaction")
            .Mark();
        getResult().result.code(TransactionResultCode::txTOO_EARLY);
        return false;
    }
    if (mEnvelope.tx.timeBounds.maxTime < closeTime ||
        mEnvelope.tx.timeBounds.maxTime - closeTime > lm.getTxExpirationPeriod())
    {
        app.getMetrics()
            .NewMeter({"transaction", "invalid", "too-late"}, "transaction")
            .Mark();
        getResult().result.code(TransactionResultCode::txTOO_LATE);
        return false;
    }

	auto& db = app.getDatabase();
    
    if (!loadAccount(delta, db))
    {
        app.getMetrics()
            .NewMeter({"transaction", "invalid", "no-account"}, "transaction")
            .Mark();
        getResult().result.code(TransactionResultCode::txNO_ACCOUNT);
        return false;
    }

	// error code already set
    if (!doCheckSignature(app, db, *mSigningAccount))
    {
        return false;
    }

    return true;
}

void
TransactionFrame::setSourceAccountPtr(AccountFrame::pointer signingAccount)
{
    if (!signingAccount)
    {
        if (!(mEnvelope.tx.sourceAccount == signingAccount->getID()))
        {
            throw std::invalid_argument("wrong account");
        }
    }
    mSigningAccount = signingAccount;
}

void
TransactionFrame::resetSignatureTracker()
{
    mSigningAccount.reset();
	auto signatureValidator = getSignatureValidator();
	signatureValidator->resetSignatureTracker();
}

bool
TransactionFrame::checkAllSignaturesUsed()
{
	auto signatureValidator = getSignatureValidator();
	if (signatureValidator->checkAllSignaturesUsed())
		return true;

	getResult().result.code(TransactionResultCode::txBAD_AUTH_EXTRA);
	return false;
}

bool
TransactionFrame::checkValid(Application& app)
{
    resetSignatureTracker();
    resetResults();
    bool res = commonValid(app, nullptr);
    if (!res)
    {    
		return res;
    }

	for (auto& op : mOperations)
	{
		if (!op->checkValid(app))
		{
			// it's OK to just fast fail here and not try to call
			// checkValid on all operations as the resulting object
			// is only used by applications
			app.getMetrics()
				.NewMeter({ "transaction", "invalid", "invalid-op" },
					"transaction")
				.Mark();
			markResultFailed();
			return false;
		}
	}
	res = checkAllSignaturesUsed();
	if (!res)
	{
		app.getMetrics()
			.NewMeter({ "transaction", "invalid", "bad-auth-extra" },
				"transaction")
			.Mark();
		return false;
	}

	string txIDString(binToHex(getContentsHash()));
	if (TransactionFrame::timingExists(app.getDatabase(), txIDString))
	{
		app.getMetrics()
			.NewMeter({ "transaction", "invalid", "duplication" }, "transaction")
			.Mark();
		getResult().result.code(TransactionResultCode::txDUPLICATION);
		return false;
	}

    return res;
}

void
TransactionFrame::markResultFailed()
{
    // changing "code" causes the xdr struct to be deleted/re-created
    // As we want to preserve the results, we save them inside a temp object
    // Also, note that because we're using move operators
    // mOperations are still valid (they have pointers to the individual
    // results elements)
    xdr::xvector<OperationResult> t(std::move(getResult().result.results()));
    getResult().result.code(TransactionResultCode::txFAILED);
    getResult().result.results() = std::move(t);

    // sanity check in case some implementations decide
    // to not implement std::move properly
    auto const& allResults = getResult().result.results();
    assert(allResults.size() == mOperations.size());
    for (size_t i = 0; i < mOperations.size(); i++)
    {
        assert(&mOperations[i]->getResult() == &allResults[i]);
    }
}

bool
TransactionFrame::apply(LedgerDelta& delta, Application& app)
{
    TransactionMeta tm;
    return apply(delta, tm, app);
}

bool
TransactionFrame::apply(LedgerDelta& delta, TransactionMeta& meta,
                        Application& app)
{
    resetSignatureTracker();
    if (!commonValid(app, &delta))
    {
        return false;
    }


    bool errorEncountered = false;

    {
        // shield outer scope of any side effects by using
        // a sql transaction for ledger state and LedgerDelta
        soci::transaction sqlTx(app.getDatabase().getSession());
        LedgerDelta thisTxDelta(delta);

        auto& opTimer =
            app.getMetrics().NewTimer({"transaction", "op", "apply"});

        for (auto& op : mOperations)
        {
            auto time = opTimer.TimeScope();
            LedgerDelta opDelta(thisTxDelta);
            bool txRes = op->apply(opDelta, app);

            if (!txRes)
            {
                errorEncountered = true;
            }
            meta.operations().emplace_back(opDelta.getChanges());
            opDelta.commit();
        }

        if (!errorEncountered)
        {
            if (!checkAllSignaturesUsed())
            {
                // this should never happen: malformed transaction should not be
                // accepted by nodes
                return false;
            }

            sqlTx.commit();
            thisTxDelta.commit();
        }
    }

    if (errorEncountered)
    {
        meta.operations().clear();
        markResultFailed();
    }

    return !errorEncountered;
}

StellarMessage
TransactionFrame::toStellarMessage() const
{
    StellarMessage msg;
    msg.type(MessageType::TRANSACTION);
    msg.transaction() = mEnvelope;
    return msg;
}

void
TransactionFrame::storeTransaction(LedgerManager& ledgerManager,
                                   TransactionMeta& tm, int txindex,
                                   TransactionResultSet& resultSet) const
{
    auto txBytes(xdr::xdr_to_opaque(mEnvelope));

    resultSet.results.emplace_back(getResultPair());
    auto txResultBytes(xdr::xdr_to_opaque(resultSet.results.back()));

    std::string txBody;
    txBody = bn::encode_b64(txBytes);

    std::string txResult;
    txResult = bn::encode_b64(txResultBytes);

    xdr::opaque_vec<> txMeta(xdr::xdr_to_opaque(tm));

    std::string meta;
    meta = bn::encode_b64(txMeta);

    string txIDString(binToHex(getContentsHash()));

    auto& db = ledgerManager.getDatabase();
    auto prep = db.getPreparedStatement(
        "INSERT INTO txhistory "
        "( txid, ledgerseq, txindex,  txbody, txresult, txmeta) VALUES "
        "(:id,  :seq,      :txindex, :txb,   :txres,   :meta)");

    auto& st = prep.statement();
    st.exchange(soci::use(txIDString));
    st.exchange(soci::use(ledgerManager.getCurrentLedgerHeader().ledgerSeq));
    st.exchange(soci::use(txindex));
    st.exchange(soci::use(txBody));
    st.exchange(soci::use(txResult));
    st.exchange(soci::use(meta));
    st.define_and_bind();
    {
        auto timer = db.getInsertTimer("txhistory");
        st.execute(true);
    }

    if (st.get_affected_rows() != 1)
    {
        throw std::runtime_error("Could not update data in SQL");
    }
}

void
TransactionFrame::processSeqNum()
{
    resetSignatureTracker();
    resetResults();
}


void
TransactionFrame::storeTransactionTiming(LedgerManager& ledgerManager,
                                      uint64 maxTime) const
{    
    string txIDString(binToHex(getContentsHash()));

    auto& db = ledgerManager.getDatabase();
    auto prep = db.getPreparedStatement(
        "INSERT INTO txtiming "
        "( txid, valid_before) VALUES "
        "(:id,  :vb)");

    auto& st = prep.statement();
    st.exchange(soci::use(txIDString));
    st.exchange(soci::use(maxTime));
    st.define_and_bind();
    {
        auto timer = db.getInsertTimer("txtiming");
        st.execute(true);
    }

    if (st.get_affected_rows() != 1)
    {
        throw std::runtime_error("Could not update data in SQL");
    }
}


void
TransactionFrame::storeTransactionFee(LedgerManager& ledgerManager,
                                      LedgerEntryChanges const& changes,
                                      int txindex) const
{
    xdr::opaque_vec<> txChanges(xdr::xdr_to_opaque(changes));

    std::string txChanges64;
    txChanges64 = bn::encode_b64(txChanges);

    string txIDString(binToHex(getContentsHash()));

    auto& db = ledgerManager.getDatabase();
    auto prep = db.getPreparedStatement(
        "INSERT INTO txfeehistory "
        "( txid, ledgerseq, txindex,  txchanges) VALUES "
        "(:id,  :seq,      :txindex, :txchanges)");

    auto& st = prep.statement();
    st.exchange(soci::use(txIDString));
    st.exchange(soci::use(ledgerManager.getCurrentLedgerHeader().ledgerSeq));
    st.exchange(soci::use(txindex));
    st.exchange(soci::use(txChanges64));
    st.define_and_bind();
    {
        auto timer = db.getInsertTimer("txfeehistory");
        st.execute(true);
    }

    if (st.get_affected_rows() != 1)
    {
        throw std::runtime_error("Could not update data in SQL");
    }
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
    txSet.previousLedgerHash() = lh->mHeader.previousLedgerHash;
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
	auto prep =
		db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM txtiming WHERE txid=:id)");
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
            make_shared<TransactionFrame>(networkID, tx);
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

    db.getSession() << "CREATE TABLE txtiming ("
                       "txid        CHARACTER(64) NOT NULL,"
                       "valid_before   BIGINT NOT NULL CHECK (valid_before >= 0),"
                       "PRIMARY KEY (txid)"
                       ")";


    db.getSession() << "CREATE INDEX histfeebyseq ON txfeehistory (ledgerseq);";
}

void
TransactionFrame::deleteOldEntries(Database& db, uint32_t ledgerSeq, uint64 ledgerCloseTime)
{
    db.getSession() << "DELETE FROM txhistory WHERE ledgerseq <= " << ledgerSeq;
    db.getSession() << "DELETE FROM txfeehistory WHERE ledgerseq <= "
                    << ledgerSeq;
    db.getSession() << "DELETE FROM txtiming WHERE valid_before < "
                    << ledgerCloseTime;
}
}
