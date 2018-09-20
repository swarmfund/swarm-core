#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/Timer.h"
#include "Application.h"
#include "main/Config.h"
#include "main/PersistentState.h"
#include <thread>
#include "medida/timer_context.h"

namespace medida {
    class Counter;

    class Timer;
}

namespace stellar {
    class TmpDirManager;

    class LedgerManager;

    class Herder;

    class BucketManager;

    class HistoryManager;

    class Invariant;

    class ProcessManager;

    class CommandHandler;

    class Database;

    class LoadGenerator;

    class NtpSynchronizationChecker;

    class ApplicationImpl : public Application {
    public:
        ApplicationImpl(VirtualClock &clock, Config cfg);

        virtual ~ApplicationImpl() override;

        virtual uint64_t timeNow() override;

        virtual Config const &getConfig() override;

        virtual State getState() const override;

        virtual std::string getStateHuman() const override;

        virtual bool isStopping() const override;

        virtual VirtualClock &getClock() override;

        virtual medida::MetricsRegistry &getMetrics() override;

        virtual void syncOwnMetrics() override;

        virtual void syncAllMetrics() override;

        virtual TmpDirManager &getTmpDirManager() override;

        virtual LedgerManager &getLedgerManager() override;

        virtual BucketManager &getBucketManager() override;

        virtual HistoryManager &getHistoryManager() override;

        virtual ProcessManager &getProcessManager() override;

        virtual Herder &getHerder() override;

        virtual Invariants& getInvariants() override;

        virtual OverlayManager &getOverlayManager() override;

        virtual Database &getDatabase() override;

        virtual PersistentState &getPersistentState() override;

        virtual CommandHandler &getCommandHandler() override;

        virtual WorkManager &getWorkManager() override;

        virtual BanManager &getBanManager() override;

        virtual StatusManager &getStatusManager() override;

        virtual asio::io_service &getWorkerIOService() override;

        void newDB() override;

        virtual void start() override;

        // Stops the worker io_service, which should cause the threads to exit once
        // they finish running any work-in-progress. If you want a more abrupt exit
        // than this, call exit() and hope for the best.
        virtual void gracefulStop() override;

        // Wait-on and join all the threads this application started; should only
        // return when there is no more work to do or someone has force-stopped the
        // worker io_service. Application can be safely destroyed after this
        // returns.
        virtual void joinAllThreads() override;

        virtual bool manualClose() override;

        virtual void generateLoad(uint32_t nAccounts, uint32_t nTxs,
                                  uint32_t txRate, bool autoRate) override;

        virtual LoadGenerator &getLoadGenerator() override;

        virtual void checkDB() override;

        virtual void checkDBSync() override;

        virtual void maintenance() override;

        virtual void applyCfgCommands() override;

        virtual void reportCfgMetrics() override;

        virtual void reportInfo() override;

        virtual Hash const &getNetworkID() const override;

        virtual AccountID getMasterID() const override;

        virtual AccountID getCommissionID() const override;

        virtual AccountID getOperationalID() const override;

        virtual std::vector<PublicKey> getSystemAccounts() const override;

        virtual std::string getBaseExchangeName() const override;

        virtual uint64 getTxExpirationPeriod() const override;

        virtual uint64 getWithdrawalDetailsMaxLength() const override;

		virtual uint64 getIssuanceDetailsMaxLength() const override;

		virtual uint64 getRejectReasonMaxLength() const override;

        virtual int64 getMaxInvoicesForReceiverAccount() const override;

        virtual uint64 getMaxInvoiceDetailLength() const override;

        virtual uint64 getMaxContractsForContractor() const override;

        virtual uint64 getMaxContractDetailLength() const override;

        virtual uint64 getMaxContractInitialDetailLength() const override;

        virtual int32 getKYCSuperAdminMask() const override;

        bool isCheckingPolicies() const override;
        void stopCheckingPolicies() override;
        void resumeCheckingPolicies() override;

    private:
        VirtualClock &mVirtualClock;
        Config mConfig;

        // NB: The io_service should come first, then the 'manager' sub-objects,
        // then the threads. Do not reorder these fields.
        //
        // The fields must be constructed in this order, because the subsystem
        // objects need to be fully instantiated before any workers acquire
        // references to them.
        //
        // The fields must be destructed in the reverse order because all worker
        // threads must be joined and destroyed before we start tearing down
        // subsystems.

        asio::io_service mWorkerIOService;
        std::unique_ptr<asio::io_service::work> mWork;

        std::unique_ptr<Database> mDatabase;
        std::unique_ptr<TmpDirManager> mTmpDirManager;
        std::unique_ptr<OverlayManager> mOverlayManager;
        std::unique_ptr<LedgerManager> mLedgerManager;
        std::unique_ptr<Herder> mHerder;
        std::unique_ptr<BucketManager> mBucketManager;
        std::unique_ptr<HistoryManager> mHistoryManager;
        std::shared_ptr<ProcessManager> mProcessManager;
        std::unique_ptr<CommandHandler> mCommandHandler;
        std::shared_ptr<WorkManager> mWorkManager;
        std::unique_ptr<PersistentState> mPersistentState;
        std::unique_ptr<LoadGenerator> mLoadGenerator;
        std::unique_ptr<BanManager> mBanManager;
        std::shared_ptr<NtpSynchronizationChecker> mNtpSynchronizationChecker;
        std::unique_ptr<StatusManager> mStatusManager;
        std::unique_ptr<Invariants> mInvariants;

        std::vector<std::thread> mWorkerThreads;

        asio::signal_set mStopSignals;

        bool mStopping;

        VirtualTimer mStoppingTimer;

        std::unique_ptr<medida::MetricsRegistry> mMetrics;
        medida::Counter &mAppStateCurrent;
        medida::Timer &mAppStateChanges;
        VirtualClock::time_point mLastStateChange;

        Hash mNetworkID;

        AccountID masterID;
        AccountID commissionID;

        std::unordered_set<ExternalSystemIDGeneratorType> mAvailableExternalSystemIDGenerators;

        bool mIsCheckingPolicies{ true };

        void shutdownMainIOService();

        void runWorkerThread(unsigned i);

        std::vector<std::unique_ptr<Invariant>> enabledInvariants();
    public:
        bool areAllExternalSystemGeneratorsAvailable(
            xdr::xvector<ExternalSystemIDGeneratorType> ex) const override;
        void addAvailableExternalSystemGenerator(
            ExternalSystemIDGeneratorType ex) override;
        const std::unordered_set<ExternalSystemIDGeneratorType>&
        getAvailableExternalSystemGenerator() override;
        const std::string getBTCAddressRoot() const override;
        const std::string getETHAddressRoot() const override;
    };
}
