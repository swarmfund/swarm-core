#pragma once

#include "ExternalSystemAccountID.h"
#include "ledger/ExternalSystemAccountIDHelper.h"
#include "util/NonCopyable.h"

namespace soci
{
class session;
}

namespace stellar
{
class StorageHelper;

class ExternalSystemAccountIDHelperImpl : public ExternalSystemAccountIDHelper,
                                          NonCopyable
{
  public:
    explicit ExternalSystemAccountIDHelperImpl(StorageHelper& storageHelper);

  private:
    void dropAll() override;
    void storeAdd(LedgerEntry const& entry) override;
    void storeChange(LedgerEntry const& entry) override;
    void storeDelete(LedgerKey const& key) override;
    bool exists(LedgerKey const& key) override;
    LedgerKey getLedgerKey(LedgerEntry const& from) override;
    EntryFrame::pointer storeLoad(LedgerKey const& key) override;
    EntryFrame::pointer fromXDR(LedgerEntry const& from) override;
    uint64_t countObjects() override;

    bool exists(AccountID accountID, int32 externalSystemType) override;

    std::vector<ExternalSystemAccountIDFrame::pointer> loadAll() override;

    // load - loads external system account ID by accountID and
    // externalSystemType. If not found returns nullptr.
    ExternalSystemAccountIDFrame::pointer
    load(const AccountID accountID, const int32 externalSystemType) override;

    Database& getDatabase() override;

  private:
    static const char* select;

    void storeUpdateHelper(bool insert, LedgerEntry const& entry);
    void load(StatementContext& prep,
              std::function<void(LedgerEntry const&)> processor);

    StorageHelper& mStorageHelper;
};

} // namespace stellar