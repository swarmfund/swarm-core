#pragma once

#include "ledger/LedgerManager.h"

namespace stellar
{
class MockLedgerManager : public LedgerManager
{
  public:

	MOCK_METHOD1(setState, void(State));
    MOCK_CONST_METHOD0(getState, State());
    MOCK_CONST_METHOD0(getStateHuman, std::string());

	MOCK_METHOD1(externalizeValue, void(LedgerCloseData const&));
    MOCK_CONST_METHOD0(getCurrentLedgerHeader, LedgerHeader const&());

    MOCK_CONST_METHOD0(getLastClosedLedgerHeader, LedgerHeaderHistoryEntry const&());
    MOCK_CONST_METHOD0(getLedgerNum, uint32_t());
    MOCK_CONST_METHOD0(getLastClosedLedgerNum, uint32_t());

    MOCK_CONST_METHOD1(getMinBalance, int64_t(uint32_t));
    MOCK_CONST_METHOD0(getCloseTime, uint64_t());
    MOCK_CONST_METHOD0(getTmCloseTime, tm());

    MOCK_CONST_METHOD0(getTxFee, int64_t());
    MOCK_CONST_METHOD0(getMaxTxSetSize, uint32_t());
    MOCK_CONST_METHOD0(getTxExpirationPeriod, uint64());
    MOCK_CONST_METHOD0(secondsSinceLastLedgerClose, uint64_t());
    MOCK_METHOD0(syncMetrics, void());
    MOCK_METHOD0(getCurrentLedgerHeader, LedgerHeader&());
    MOCK_METHOD0(getDatabase, Database&());
    MOCK_METHOD0(startNewLedger, void());
    MOCK_METHOD1(loadLastKnownLedger, void(std::function<void(asio::error_code const& ec)>));
    MOCK_METHOD3(startCatchUp, void(uint32_t, HistoryManager::CatchupMode, bool));
    MOCK_CONST_METHOD1(verifyCatchupCandidate, HistoryManager::VerifyHashStatus(LedgerHeaderHistoryEntry const&));
    MOCK_METHOD1(closeLedger, void(LedgerCloseData const&));
    MOCK_METHOD3(deleteOldEntries, void(Database&, uint32_t, uint64_t));
    MOCK_METHOD0(checkDbState, void());
    MOCK_METHOD1(shouldUse, bool(LedgerVersion const));
};
}