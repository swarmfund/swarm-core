#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <map>
#include <memory>
#include "ledger/LedgerManager.h"
#include "ledger/AccountFrame.h"
#include "overlay/StellarXDR.h"
#include "transactions/SignatureValidator.h"
#include "ledger/LedgerDelta.h"
#include "util/types.h"

namespace soci
{
class session;
}

/*
A transaction in its exploded form.
We can get it in from the DB or from the wire
*/
namespace stellar
{
class Application;
class OperationFrame;
class LedgerDelta;
class SecretKey;
class XDROutputFileStream;
class SHA256;

class TransactionFrame;
typedef std::shared_ptr<TransactionFrame> TransactionFramePtr;

class TransactionFrame
{
  private:
	SignatureValidator::pointer mSignatureValidator;

  protected:
    TransactionEnvelope mEnvelope;
    TransactionResult mResult;

    AccountFrame::pointer mSigningAccount;

    Hash const& mNetworkID;     // used to change the way we compute signatures
    mutable Hash mContentsHash; // the hash of the contents
    mutable Hash mFullHash;     // the hash of the contents and the sig.

    std::vector<std::shared_ptr<OperationFrame>> mOperations;

    bool loadAccount(LedgerDelta* delta, Database& app);
    bool commonValid(Application& app, LedgerDelta* delta);

	bool checkAllSignaturesUsed();
	void resetSignatureTracker();
    void resetResults();
    void markResultFailed();

    bool applyTx(LedgerDelta& delta, TransactionMeta& meta, Application& app, std::vector<LedgerDelta::KeyEntryMap>& stateBeforeOp);
    static void unwrapNestedException(const std::exception& e, std::stringstream& str);

  public:
    TransactionFrame(Hash const& networkID,
                     TransactionEnvelope const& envelope);
    TransactionFrame(TransactionFrame const&) = delete;
    TransactionFrame() = delete;

    static TransactionFramePtr
    makeTransactionFromWire(Hash const& networkID,
                            TransactionEnvelope const& msg);

    Hash const& getFullHash() const;
    Hash const& getContentsHash() const;

	SignatureValidator::pointer getSignatureValidator()
	{
		if (!mSignatureValidator)
		{
			mSignatureValidator = std::make_shared<SignatureValidator>(getContentsHash(), getEnvelope().signatures);
		}

		return mSignatureValidator;
	}

    AccountFrame::pointer
    getSourceAccountPtr() const
    {
        return mSigningAccount;
    }

    void setSourceAccountPtr(AccountFrame::pointer signingAccount);

    std::vector<std::shared_ptr<OperationFrame>> const&
    getOperations() const
    {
        return mOperations;
    }

    TransactionResult const&
    getResult() const
    {
        return mResult;
    }

    TransactionResult&
    getResult()
    {
        return mResult;
    }

    TransactionResultCode
    getResultCode() const
    {
        return getResult().result.code();
    }

    TransactionResultPair getResultPair() const;
    TransactionEnvelope const& getEnvelope() const;
    TransactionEnvelope& getEnvelope();

    Salt
    getSalt() const
    {
        return mEnvelope.tx.salt;
    }

    AccountFrame const&
    getSourceAccount() const
    {
        assert(mSigningAccount);
        return *mSigningAccount;
    }


    AccountID const&
    getSourceID() const
    {
        return mEnvelope.tx.sourceAccount;
    }


    TimeBounds
    getTimeBounds() const
    {
        return mEnvelope.tx.timeBounds;
    }


    int64_t getPaidFee() const;

    void addSignature(SecretKey const& secretKey);

	// Checks signature, if not valid - returns false and sets valid error code
    bool doCheckSignature(Application& app, Database& db, AccountFrame& account);

    bool checkValid(Application& app);

    void processSeqNum();


    // apply this transaction to the current ledger
    // returns true if successfully applied
    bool apply(LedgerDelta& delta, TransactionMeta& meta, Application& app, std::vector<LedgerDelta::KeyEntryMap>& stateBeforeOp);

    // version without meta
    bool apply(LedgerDelta& delta, Application& app);

    StellarMessage toStellarMessage() const;

    AccountFrame::pointer loadAccount(LedgerDelta* delta, Database& app,
                                      AccountID const& accountID);


    // transaction history
    void storeTransaction(LedgerManager& ledgerManager, TransactionMeta& tm,
                          int txindex, TransactionResultSet& resultSet) const;

    // fee history
    void storeTransactionFee(LedgerManager& ledgerManager,
                             LedgerEntryChanges const& changes,
                             int txindex) const;

    void storeTransactionTiming(LedgerManager& ledgerManager,
                                      uint64 maxTime) const;


    // transaction fee
    bool processTxFee(Application& app, LedgerDelta* delta);

    bool tryGetTxFeeAsset(Database& db, AssetCode& txFeeAssetCode);

    void storeFeeForOpType(OperationType opType, std::map<OperationType, uint64_t>& feesForOpTypes,
                           AccountFrame::pointer source, AssetCode txFeeAssetCode, Database& db);


    // access to history tables
    static TransactionResultSet getTransactionHistoryResults(Database& db,
                                                             uint32 ledgerSeq);
    static std::vector<LedgerEntryChanges>
    getTransactionFeeMeta(Database& db, uint32 ledgerSeq);
    
    static bool timingExists(Database& db, std::string txID);

    /*
    txOut: stream of TransactionHistoryEntry
    txResultOut: stream of TransactionHistoryResultEntry
    */
    static size_t copyTransactionsToStream(Hash const& networkID, Database& db,
                                           soci::session& sess,
                                           uint32_t ledgerSeq,
                                           uint32_t ledgerCount,
                                           XDROutputFileStream& txOut,
                                           XDROutputFileStream& txResultOut);
    static void dropAll(Database& db);

    static void deleteOldEntries(Database& db, uint32_t ledgerSeq,
        uint64 ledgerCloseTime);

	void clearCached();
};
}
