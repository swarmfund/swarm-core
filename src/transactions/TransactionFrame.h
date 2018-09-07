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
  public:
    static TransactionFramePtr
    makeTransactionFromWire(Hash const& networkID,
                            TransactionEnvelope const& msg);

    virtual Hash const& getFullHash() const = 0;
    virtual Hash const& getContentsHash() const = 0;

    virtual SignatureValidator::pointer getSignatureValidator() = 0;
    virtual AccountFrame::pointer getSourceAccountPtr() const = 0;

    virtual void setSourceAccountPtr(AccountFrame::pointer signingAccount) = 0;

    virtual std::vector<std::shared_ptr<OperationFrame>> const& getOperations() const = 0;
    virtual TransactionResult const& getResult() const = 0;
    virtual TransactionResult& getResult() = 0;

    virtual TransactionResultCode getResultCode() const = 0;

    virtual TransactionResultPair getResultPair() const = 0;
    virtual TransactionEnvelope const& getEnvelope() const = 0;
    virtual TransactionEnvelope& getEnvelope() = 0;

    virtual Salt getSalt() const = 0;

    virtual AccountFrame const& getSourceAccount() const = 0;

    virtual AccountID const& getSourceID() const = 0;
    virtual TimeBounds getTimeBounds() const = 0;
    virtual int64_t getPaidFee() const = 0;

    virtual void addSignature(SecretKey const& secretKey) = 0;

	// Checks signature, if not valid - returns false and sets valid error code
    virtual bool doCheckSignature(Application& app, Database& db, AccountFrame& account) = 0;

    virtual bool checkValid(Application& app) = 0;

    virtual void processSeqNum() = 0;


    // apply this transaction to the current ledger
    // returns true if successfully applied
    virtual bool
    apply(LedgerDelta& delta, TransactionMeta& meta, Application& app,
          std::vector<LedgerDelta::KeyEntryMap>& stateBeforeOp) = 0;

    // version without meta
    virtual bool apply(LedgerDelta& delta, Application& app) = 0;

    virtual StellarMessage toStellarMessage() const = 0;

    virtual AccountFrame::pointer loadAccount(LedgerDelta* delta, Database& app,
                                              AccountID const& accountID) = 0;


    // transaction history
    virtual void storeTransaction(LedgerManager& ledgerManager,
                                  TransactionMeta& tm, int txindex,
                                  TransactionResultSet& resultSet) const = 0;

    // fee history
    virtual void storeTransactionFee(LedgerManager& ledgerManager,
                                     LedgerEntryChanges const& changes,
                                     int txindex) const = 0;

    virtual void storeTransactionTiming(LedgerManager& ledgerManager,
                                        uint64 maxTime) const = 0;

    // transaction fee
    virtual bool processTxFee(Application& app, LedgerDelta* delta) = 0;

    virtual bool tryGetTxFeeAsset(Database& db, AssetCode& txFeeAssetCode) = 0;

    virtual void storeFeeForOpType(OperationType opType, std::map<OperationType, uint64_t>& feesForOpTypes,
                                   AccountFrame::pointer source, AssetCode txFeeAssetCode, Database& db) = 0;


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

	virtual void clearCached() = 0;
};
}
