#pragma once

#include "ledger/ExternalSystemAccountIDHelper.h"

namespace stellar
{

class MockExternalSystemAccountIDHelper : public ExternalSystemAccountIDHelper
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
    MOCK_METHOD2(exists, bool(AccountID accountID, int32 externalSystemType));
    MOCK_METHOD0(loadAll, std::vector<ExternalSystemAccountIDFrame::pointer>());
    MOCK_METHOD2(load, ExternalSystemAccountIDFrame::pointer(
                           const AccountID accountID,
                           const int32 externalSystemType));
};

} // namespace stellar
