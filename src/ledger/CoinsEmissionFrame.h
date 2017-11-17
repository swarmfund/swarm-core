#pragma once

#include "ledger/EntryFrame.h"
#include <functional>
#include <unordered_map>

namespace soci
{
class session;
}

namespace stellar
{
class StatementContext;

class CoinsEmissionFrame : public EntryFrame
{
    static void
    loadCoinsEmissions(StatementContext& prep,
               std::function<void(LedgerEntry const&)> coinsEmissionProcessor);

    CoinsEmissionEntry& mCoinsEmission;

    CoinsEmissionFrame(CoinsEmissionFrame const& from);

    void storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert);

  public:
    typedef std::shared_ptr<CoinsEmissionFrame> pointer;

    CoinsEmissionFrame();
    CoinsEmissionFrame(LedgerEntry const& from);

    CoinsEmissionFrame& operator=(CoinsEmissionFrame const& other);

    EntryFrame::pointer
    copy() const override
    {
        return EntryFrame::pointer(new CoinsEmissionFrame(*this));
    }

    CoinsEmissionEntry const&
    getCoinsEmission() const
    {
        return mCoinsEmission;
    }
    CoinsEmissionEntry&
    getCoinsEmission()
    {
        return mCoinsEmission;
    }


    uint64 getAmount()
    {
        return mCoinsEmission.amount;
    }
    
    std::string getSerialNumber()
    {
        return mCoinsEmission.serialNumber;
    }
    
    std::string getAsset()
    {
        return mCoinsEmission.asset;
    }
    
    static bool isValid(CoinsEmissionEntry const& oe);
    bool isValid() const;

    // Instance-based overrides of EntryFrame.
    void storeDelete(LedgerDelta& delta, Database& db) const override;
    void storeChange(LedgerDelta& delta, Database& db) override;
    void storeAdd(LedgerDelta& delta, Database& db) override;

    // Static helpers that don't assume an instance.
    static void storeDelete(LedgerDelta& delta, Database& db,
                            LedgerKey const& key);
	static bool exists(Database& db, LedgerKey const& key);
	static bool exists(Database& db, std::string serialNumber);
    static bool exists(Database& db, std::vector<std::string> serialNumbers);
    static uint64_t countObjects(soci::session& sess);

    // database utilities
    static pointer loadCoinsEmission(std::string serialNumber,
                             Database& db, LedgerDelta* delta = nullptr);

    static void storePreEmissions(std::vector<std::shared_ptr<CoinsEmissionFrame>> preEmissions, LedgerDelta& delta, Database& db);

    static void dropAll(Database& db);
    static const char* kSQLCreateStatement1;
};
}
