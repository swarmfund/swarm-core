
#pragma once

#include "EntityTypeFrame.h"
#include "ledger/EntryHelper.h"
#include "map"
#include "xdr/Stellar-ledger-entries-account.h"
#include <functional>
#include <unordered_map>

namespace stellar
{
class LedgerManager;

class EntityTypeHelper : public EntryHelper
{
  public:
    EntityTypeHelper(EntityTypeHelper const&) = delete;
    EntityTypeHelper& operator=(EntityTypeHelper const&) = delete;

    static EntityTypeHelper*
    Instance()
    {
        static EntityTypeHelper singleton;
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
    bool exists(Database& db, uint64_t id, EntityType type);

    uint64_t countObjects(soci::session& sess) override;
    LedgerKey getLedgerKey(LedgerEntry const& from) override;
    EntryFrame::pointer fromXDR(LedgerEntry const& from) override;
    EntryFrame::pointer storeLoad(LedgerKey const& key, Database& db) override;

    EntityTypeFrame::pointer loadEntityType(uint64_t id, EntityType type,
                                            Database& db,
                                            LedgerDelta* delta = nullptr);
    std::vector<EntityTypeFrame::pointer> loadEntityTypes(uint64_t id,
                                                          Database& db);

  private:
    EntityTypeHelper() = default;

    void storeUpdate(LedgerDelta& delta, Database& db, bool insert,
                     LedgerEntry const& entry);
};

} // namespace stellar
