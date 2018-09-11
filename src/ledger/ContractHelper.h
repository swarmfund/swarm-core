#pragma once

#include "EntryHelperLegacy.h"
#include "ContractFrame.h"
#include <functional>
#include <unordered_map>

namespace soci
{
    class session;
}

namespace stellar
{
class StatementContext;

class ContractHelper : public EntryHelperLegacy
{
public:
    static ContractHelper *Instance()
    {
        static ContractHelper singleton;
        return &singleton;
    }

    ContractHelper(ContractHelper const&) = delete;
    ContractHelper& operator=(ContractHelper const&) = delete;

    void addCustomerDetails(Database &db);
    void dropAll(Database& db) override;
    void storeAdd(LedgerDelta& delta, Database& db, LedgerEntry const& entry) override;
    void storeChange(LedgerDelta& delta, Database& db, LedgerEntry const& entry) override;
    void storeDelete(LedgerDelta& delta, Database& db, LedgerKey const& key) override;
    bool exists(Database& db, LedgerKey const& from) override;
    LedgerKey getLedgerKey(LedgerEntry const& from) override;
    EntryFrame::pointer storeLoad(LedgerKey const& key, Database& db) override;
    EntryFrame::pointer fromXDR(LedgerEntry const& from) override;
    uint64_t countObjects(soci::session& sess) override;

    ContractFrame::pointer loadContract(uint64_t id, Database& db, LedgerDelta* delta = nullptr);
    uint64_t countContracts(AccountID const& contractor, Database& db);

private:
    ContractHelper() { ; }
    ~ContractHelper() { ; }

    void load(StatementContext & prep, std::function<void(LedgerEntry const&)> processor);
    void storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert, LedgerEntry const& entry);
};

}
