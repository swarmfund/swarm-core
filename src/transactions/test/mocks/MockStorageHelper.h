#pragma once

#include "ledger/StorageHelper.h"

namespace stellar
{

class MockStorageHelper : public StorageHelper
{
public:
    MOCK_METHOD0(getDatabase, Database&());
    MOCK_CONST_METHOD0(getDatabase, const Database&());
    MOCK_METHOD0(getLedgerDelta, LedgerDelta&());
    MOCK_CONST_METHOD0(getLedgerDelta, const LedgerDelta&());
    MOCK_METHOD0(commit, void());
    MOCK_METHOD0(rollback, void());
    MOCK_METHOD0(release, void());
    MOCK_METHOD0(startNestedTransaction, std::unique_ptr<StorageHelper>());
    MOCK_METHOD0(getKeyValueHelper, KeyValueHelper&());
    MOCK_METHOD0(getBalanceHelper, BalanceHelper&());
    MOCK_METHOD0(getAssetHelper, AssetHelper&());
    MOCK_METHOD0(getExternalSystemAccountIDHelper,
                 ExternalSystemAccountIDHelper&());
    MOCK_METHOD0(getExternalSystemAccountIDPoolEntryHelper,
                 ExternalSystemAccountIDPoolEntryHelper&());
};

}  // namespace stellar