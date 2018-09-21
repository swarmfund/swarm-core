#pragma once

#include "transactions/TransactionFrame.h"

namespace stellar
{

class MockTransactionFrame : public TransactionFrame
{
  public:
    MOCK_CONST_METHOD0(getFullHash, const Hash&());
    MOCK_CONST_METHOD0(getContentsHash, const Hash&());
    MOCK_METHOD0(getSignatureValidator, SignatureValidator::pointer());
    MOCK_CONST_METHOD0(getSourceAccountPtr, AccountFrame::pointer());
    MOCK_METHOD1(setSourceAccountPtr,
                 void(AccountFrame::pointer signingAccount));
    MOCK_CONST_METHOD0(getOperations,
                       const std::vector<std::shared_ptr<OperationFrame>>&());
    MOCK_CONST_METHOD0(getResult, const TransactionResult&());
    MOCK_METHOD0(getResult, TransactionResult&());
    MOCK_CONST_METHOD0(getResultCode, TransactionResultCode());
    MOCK_CONST_METHOD0(getResultPair, TransactionResultPair());
    MOCK_CONST_METHOD0(getEnvelope, const TransactionEnvelope&());
    MOCK_METHOD0(getEnvelope, TransactionEnvelope&());
    MOCK_CONST_METHOD0(getSalt, Salt());
    MOCK_CONST_METHOD0(getSourceAccount, const AccountFrame&());
    MOCK_CONST_METHOD0(getSourceID, const AccountID&());
    MOCK_CONST_METHOD0(getTimeBounds, TimeBounds());
    MOCK_CONST_METHOD0(getPaidFee, int64_t());
    MOCK_METHOD1(addSignature, void(SecretKey const& secretKey));
    MOCK_METHOD3(doCheckSignature,
                 bool(Application& app, Database& db, AccountFrame& account));
    MOCK_METHOD1(checkValid, bool(Application& app));
    MOCK_METHOD0(processSeqNum, void());
    MOCK_METHOD4(apply,
                 bool(LedgerDelta& delta, TransactionMeta& meta,
                      Application& app,
                      std::vector<LedgerDelta::KeyEntryMap>& stateBeforeOp));
    MOCK_METHOD2(apply, bool(LedgerDelta& delta, Application& app));
    MOCK_CONST_METHOD0(toStellarMessage, StellarMessage());
    MOCK_METHOD3(loadAccount,
                 AccountFrame::pointer(LedgerDelta* delta, Database& app,
                                       AccountID const& accountID));
    MOCK_CONST_METHOD4(storeTransaction,
                       void(LedgerManager& ledgerManager, TransactionMeta& tm,
                            int txindex, TransactionResultSet& resultSet));
    MOCK_CONST_METHOD3(storeTransactionFee,
                       void(LedgerManager& ledgerManager,
                            LedgerEntryChanges const& changes, int txindex));
    MOCK_CONST_METHOD2(storeTransactionTiming,
                       void(LedgerManager& ledgerManager, uint64 maxTime));
    MOCK_METHOD2(processTxFee, bool(Application& app, LedgerDelta* delta));
    MOCK_METHOD2(tryGetTxFeeAsset, bool(Database& db, AssetCode& txFeeAssetCode));
    MOCK_METHOD5(storeFeeForOpType,
                 void(OperationType opType,
                      std::map<OperationType, uint64_t>& feesForOpTypes,
                      AccountFrame::pointer source, AssetCode txFeeAssetCode,
                      Database& db));
    MOCK_METHOD0(clearCached, void());
};

} // namespace stellar
