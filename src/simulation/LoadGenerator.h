#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "main/Application.h"
#include "crypto/SecretKey.h"
#include "transactions/test/TxTests.h"
#include "xdr/Stellar-types.h"
#include <vector>

namespace medida
{
class MetricsRegistry;
class Meter;
class Counter;
class Timer;
}

namespace stellar
{

class VirtualTimer;

class LoadGenerator
{
  public:
    LoadGenerator(Hash const& networkID);
    ~LoadGenerator();
    void clear();

    struct TxInfo;
    struct AccountInfo;
    using AccountInfoPtr = std::shared_ptr<AccountInfo>;

    static const uint32_t STEP_MSECS;

    // Primary store of accounts.
    std::vector<AccountInfoPtr> mAccounts;

    std::unique_ptr<VirtualTimer> mLoadTimer;
    uint64_t mLastSecond;

    // Schedule a callback to generateLoad() STEP_MSECS miliseconds from now.
    void scheduleLoadGeneration(Application& app, uint32_t nAccounts,
                                uint32_t nTxs, uint32_t txRate, bool autoRate);

    // Generate one "step" worth of load (assuming 1 step per STEP_MSECS) at a
    // given target number of accounts and txs, and a given target tx/s rate.
    // If work remains after the current step, call scheduleLoadGeneration()
    // with the remainder.
    void generateLoad(Application& app, uint32_t nAccounts, uint32_t nTxs,
                      uint32_t txRate, bool autoRate);

    bool maybeCreateAccount(uint32_t ledgerNum, std::vector<TxInfo>& txs);

    std::vector<TxInfo> accountCreationTransactions(size_t n);
    AccountInfoPtr createAccount(size_t i, uint32_t ledgerNum = 0);
    std::vector<AccountInfoPtr> createAccounts(size_t n);
    bool loadAccount(Application& app, AccountInfo& account);
    bool loadAccount(Application& app, AccountInfoPtr account);
    bool loadAccounts(Application& app, std::vector<AccountInfoPtr> accounts);

    TxInfo createTransferTransaction(AccountInfoPtr from,
                                           AccountInfoPtr to, int64_t amount);

    AccountInfoPtr pickRandomAccount(AccountInfoPtr tryToAvoid,
                                     uint32_t ledgerNum);

    TxInfo createRandomTransaction(float alpha, uint32_t ledgerNum = 0);
    std::vector<TxInfo> createRandomTransactions(size_t n, float paretoAlpha);

    struct AccountInfo : public std::enable_shared_from_this<AccountInfo>
    {
        AccountInfo(size_t id, SecretKey key, int64_t balance, uint32_t lastChangedLedger,
                    LoadGenerator& loadGen);
        size_t mId;
        SecretKey mKey;
        int64_t mBalance;
        uint32_t mLastChangedLedger;

        bool canUseInLedger(uint32_t currentLedger);
		void createDirectly(Application& app);

        TxInfo creationTransaction();
		TxInfo fundTransaction();

      private:
        LoadGenerator& mLoadGen;
    };

    struct TxMetrics
    {
        medida::Meter& mAccountCreated;
        medida::Meter& mPayment;
        medida::Meter& mTxnAttempted;
        medida::Meter& mTxnRejected;
        medida::Meter& mTxnBytes;


        TxMetrics(medida::MetricsRegistry& m);
        void report();
    };

    struct TxInfo
    {
        AccountInfoPtr mFrom;
        AccountInfoPtr mTo;
        enum
        {
            TX_CREATE_ACCOUNT,
			TX_FUND_ACCOUNT,
            TX_TRANSFER,
			TX_SKIP,
        } mType;
        int64_t mAmount;

        void touchAccounts(uint32_t ledger);
        bool execute(Application& app);

        void toTransactionFrames(Application& app,
                                 std::vector<TransactionFramePtr>& txs,
                                 TxMetrics& metrics);
        void recordExecution(int64_t baseFee);
    };
};
}
