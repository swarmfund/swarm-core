#pragma once

// Copyright 2017 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "xdr/Stellar-SCP.h"
#include "xdr/Stellar-overlay.h"
#include "xdr/Stellar-ledger.h"

#include "main/Config.h"
#include "util/Timer.h"
#include "util/optional.h"
#include <stdint.h>
#include "main/Application.h"
#include <vector>

namespace stellar
{
class Config;
struct LedgerHeader;
struct LedgerUpgrade;

class Upgrades
{
  public:
    struct UpgradeParameters
    {
        UpgradeParameters()
        {
        }
        VirtualClock::time_point mUpgradeTime;
        optional<uint32> mProtocolVersion;

        std::string toJson() const;
        void fromJson(std::string const& s);
    };

    Upgrades()
    {
    }
    explicit Upgrades(UpgradeParameters const& params);

    void setParameters(UpgradeParameters const& params, Config const& cfg);

    UpgradeParameters const& getParameters() const;

    // create upgrades for given ledger
    std::vector<LedgerUpgrade>
    createUpgradesFor(LedgerHeader const &header, Application &app) const;

    // apply upgrade to ledger header
    static void applyTo(LedgerUpgrade const& upgrade, LedgerHeader& header);

    // convert upgrade value to string
    static std::string toString(LedgerUpgrade const& upgrade);

    // returns true if upgrade is a valid upgrade step
    // in which case it also sets upgradeType
    bool isValid(UpgradeType const &upgrade, LedgerUpgradeType &upgradeType, bool nomination,
                 LedgerHeader const &header, Application &app) const;

    // constructs a human readable string that represents
    // the pending upgrades
    std::string toString() const;

    // sets updated to true if some upgrades were removed
    UpgradeParameters
    removeUpgrades(std::vector<UpgradeType>::const_iterator beginUpdates,
                   std::vector<UpgradeType>::const_iterator endUpdates,
                   bool& updated);

  private:
    UpgradeParameters mParams;

    bool timeForUpgrade(uint64_t time) const;
};
}
