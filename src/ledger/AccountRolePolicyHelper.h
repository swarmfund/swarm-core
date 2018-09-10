
#pragma once

#include "AccountRolePolicyFrame.h"
#include "ledger/EntryHelper.h"
#include "map"
#include "xdr/Stellar-types.h"
#include <functional>
#include <unordered_map>

namespace stellar
{
class LedgerManager;

class AccountRolePolicyHelper : public EntryHelper
{
  public:
    AccountRolePolicyHelper(AccountRolePolicyHelper const&) = delete;
    AccountRolePolicyHelper& operator=(AccountRolePolicyHelper const&) = delete;

    static AccountRolePolicyHelper*
    Instance()
    {
        static AccountRolePolicyHelper singleton;
        return &singleton;
    }

    void dropAll(Database& db) override;

    void storeAdd(LedgerDelta& delta, Database& db,
                  LedgerEntry const& entry) override;
    void storeChange(LedgerDelta& delta, Database& db,
                     LedgerEntry const& entry) override;
    void storeDelete(LedgerDelta& delta, Database& db,
                     LedgerKey const& key) override;

    bool exists(Database& db, LedgerKey const& key) override;
    bool exists(Database& db, uint64_t id);

    uint64_t countObjects(soci::session& sess) override;
    LedgerKey getLedgerKey(LedgerEntry const& from) override;
    EntryFrame::pointer fromXDR(LedgerEntry const& from) override;
    EntryFrame::pointer storeLoad(LedgerKey const& key, Database& db) override;

    AccountRolePolicyFrame::pointer
    loadPolicy(uint64_t id, AccountID ownerID, Database &db, LedgerDelta *delta = nullptr);

    uint64_t countObjectsForOwner(const AccountID &ownerID, soci::session& sess);


  private:
    AccountRolePolicyHelper() = default;

    void storeUpdate(LedgerDelta& delta, Database& db, bool insert,
                     LedgerEntry const& entry);
};

} // namespace stellar
