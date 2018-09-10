#pragma once

#include "ledger/ExternalSystemAccountIDPoolEntryHelper.h"

namespace stellar
{

class MockExternalSystemAccountIDPoolEntryHelper
    : public ExternalSystemAccountIDPoolEntryHelper
{
  public:
    MOCK_METHOD0(dropAll, void());
    MOCK_METHOD1(storeAdd, void(LedgerEntry const& entry));
    MOCK_METHOD1(storeChange, void(LedgerEntry const& entry));
    MOCK_METHOD1(storeDelete, void(LedgerKey const& key));
    MOCK_METHOD1(exists, bool(LedgerKey const& key));
    MOCK_METHOD1(getLedgerKey, LedgerKey(LedgerEntry const& from));
    MOCK_METHOD1(fromXDR, EntryFrame::pointer(LedgerEntry const& from));
    MOCK_METHOD1(storeLoad, EntryFrame::pointer(LedgerKey const& ledgerKey));
    MOCK_METHOD0(countObjects, uint64_t());
    MOCK_METHOD0(getDatabase, Database&());
    MOCK_METHOD1(flushCachedEntry, void(LedgerKey const& key));
    MOCK_METHOD1(cachedEntryExists, bool(LedgerKey const& key));
    MOCK_METHOD0(fixTypes, void());
    MOCK_METHOD0(parentToNumeric, void());
    MOCK_METHOD1(exists, bool(uint64_t poolEntryID));
    MOCK_METHOD2(existsForAccount,
                 bool(int32 externalSystemType, AccountID accountID));
    MOCK_METHOD1(load, ExternalSystemAccountIDPoolEntryFrame::pointer(
                           uint64_t poolEntryID));
    MOCK_METHOD2(load, ExternalSystemAccountIDPoolEntryFrame::pointer(
                           int32 type, std::string data));
    MOCK_METHOD2(load, ExternalSystemAccountIDPoolEntryFrame::pointer(
                           int32 externalSystemType, AccountID accountID));
    MOCK_METHOD2(loadAvailablePoolEntry,
                 ExternalSystemAccountIDPoolEntryFrame::pointer(
                     LedgerManager& ledgerManager, int32 externalSystemType));
    MOCK_METHOD0(loadPool,
                 std::vector<ExternalSystemAccountIDPoolEntryFrame::pointer>());
};

} // namespace stellar
