#pragma once

#include "main/Application.h"

namespace medida
{
class MetricsRegistry;
}

namespace stellar
{
class Config;
class VirtualClock;
class TmpDirManager;
class LedgerManager;
class BucketManager;
class HistoryManager;
class ProcessManager;
class Herder;
class Invariants;
class OverlayManager;
class Database;
class PersistentState;
class LoadGenerator;
class CommandHandler;
class WorkManager;
class BanManager;
class StatusManager;

class MockApplication : public Application
{
  public:
    MOCK_METHOD0(timeNow, uint64_t());
    MOCK_METHOD0(getConfig, Config const&());
    MOCK_CONST_METHOD0(getState, State());
    MOCK_CONST_METHOD0(getStateHuman, std::string());
    MOCK_CONST_METHOD0(isStopping, bool());
    MOCK_METHOD0(getClock, VirtualClock&());
    MOCK_METHOD0(getMetrics, medida::MetricsRegistry&());
    MOCK_METHOD0(syncOwnMetrics, void());
    MOCK_METHOD0(syncAllMetrics, void());

    MOCK_METHOD0(getTmpDirManager, TmpDirManager&());
    MOCK_METHOD0(getLedgerManager, LedgerManager&());
    MOCK_METHOD0(getBucketManager, BucketManager&());
    MOCK_METHOD0(getHistoryManager, HistoryManager&());
    MOCK_METHOD0(getProcessManager, ProcessManager&());
    MOCK_METHOD0(getHerder, Herder&());
    MOCK_METHOD0(getInvariants, Invariants&());
    MOCK_METHOD0(getOverlayManager, OverlayManager&());
    MOCK_METHOD0(getDatabase, Database&());
    MOCK_METHOD0(getPersistentState, PersistentState&());
    MOCK_METHOD0(getCommandHandler, CommandHandler&());
    MOCK_METHOD0(getWorkManager, WorkManager&());
    MOCK_METHOD0(getBanManager, BanManager&());
    MOCK_METHOD0(getStatusManager, StatusManager&());

    MOCK_METHOD0(getWorkerIOService, asio::io_service&());

    MOCK_METHOD0(start, void());
    MOCK_METHOD0(gracefulStop, void());
    MOCK_METHOD0(joinAllThreads, void());
    MOCK_METHOD0(manualClose, bool());
    MOCK_METHOD4(generateLoad, void(uint32_t, uint32_t, uint32_t, bool));
    MOCK_METHOD0(getLoadGenerator, LoadGenerator&());

    MOCK_METHOD0(checkDB, void());
    MOCK_METHOD0(checkDBSync, void());

    MOCK_METHOD0(maintenance, void());
    MOCK_METHOD0(applyCfgCommands, void());
    MOCK_METHOD0(reportCfgMetrics, void());
    MOCK_METHOD0(reportInfo, void());
    MOCK_CONST_METHOD0(getNetworkID, Hash const&());
    MOCK_METHOD0(newDB, void());

    MOCK_CONST_METHOD0(getMasterID, AccountID());
    MOCK_CONST_METHOD0(getCommissionID, AccountID());
    MOCK_CONST_METHOD0(getOperationalID, AccountID());
    MOCK_CONST_METHOD0(getSystemAccounts, std::vector<PublicKey>());
    MOCK_CONST_METHOD0(getBaseExchangeName, std::string());
    MOCK_CONST_METHOD0(getTxExpirationPeriod, uint64());
    MOCK_CONST_METHOD0(getWithdrawalDetailsMaxLength, uint64());
    MOCK_CONST_METHOD0(getIssuanceDetailsMaxLength, uint64());
    MOCK_CONST_METHOD0(getRejectReasonMaxLength, uint64());
    MOCK_CONST_METHOD0(getMaxContractDetailLength, uint64());
    MOCK_CONST_METHOD0(getMaxContractInitialDetailLength, uint64());
    MOCK_CONST_METHOD0(getMaxContractsForContractor, uint64());
    MOCK_CONST_METHOD0(getMaxInvoiceDetailLength, uint64());
    MOCK_CONST_METHOD0(getMaxInvoicesForReceiverAccount, int64());
    MOCK_CONST_METHOD0(getKYCSuperAdminMask, int32());

    MOCK_CONST_METHOD1(areAllExternalSystemGeneratorsAvailable,
                       bool(xdr::xvector<ExternalSystemIDGeneratorType>));
    MOCK_METHOD1(addAvailableExternalSystemGenerator,
                 void(ExternalSystemIDGeneratorType));
    MOCK_METHOD0(getAvailableExternalSystemGenerator,
                 const std::unordered_set<ExternalSystemIDGeneratorType>&());

    MOCK_CONST_METHOD0(getBTCAddressRoot, const std::string());
    MOCK_CONST_METHOD0(getETHAddressRoot, const std::string());
};
} // namespace stellar