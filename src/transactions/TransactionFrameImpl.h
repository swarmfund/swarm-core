#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/TransactionFrame.h"
#include <memory>
#include "ledger/LedgerManager.h"
#include "ledger/AccountFrame.h"
#include "overlay/StellarXDR.h"
#include "transactions/SignatureValidatorImpl.h"
#include "ledger/LedgerDelta.h"
#include "util/types.h"

namespace soci
{
class session;
}

namespace stellar
{

class TransactionFrameImpl : public TransactionFrame
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
    TransactionFrameImpl(Hash const& networkID,
                         TransactionEnvelope const& envelope);
    TransactionFrameImpl(TransactionFrameImpl const&) = delete;
    TransactionFrameImpl() = delete;

    Hash const& getFullHash() const;
    Hash const& getContentsHash() const;

	SignatureValidator::pointer getSignatureValidator()
	{
		if (!mSignatureValidator)
		{
			mSignatureValidator = std::make_shared<SignatureValidatorImpl>(
			        getContentsHash(), getEnvelope().signatures);
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
    bool processTxFee(Application& app, LedgerDelta* delta) override;

    bool tryGetTxFeeAsset(Database& db, AssetCode& txFeeAssetCode) override;

    void storeFeeForOpType(OperationType opType, std::map<OperationType, uint64_t>& feesForOpTypes,
                                   AccountFrame::pointer source, AssetCode txFeeAssetCode, Database& db) override;

	void clearCached();
};
}
