// Copyright 2017 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "herder/Upgrades.h"
#include "main/Config.h"
#include "util/Logging.h"
#include "util/Timer.h"
#include <cereal/archives/json.hpp>
#include <cereal/cereal.hpp>
#include <lib/util/format.h>
#include <xdrpp/marshal.h>

namespace cereal
{
template <class Archive>
void
save(Archive& ar, stellar::Upgrades::UpgradeParameters const& p)
{
    ar(make_nvp("time", stellar::VirtualClock::to_time_t(p.mUpgradeTime)));
    ar(make_nvp("version", p.mProtocolVersion));
}

template <class Archive>
void
load(Archive& ar, stellar::Upgrades::UpgradeParameters& o)
{
    time_t t;
    ar(make_nvp("time", t));
    o.mUpgradeTime = stellar::VirtualClock::from_time_t(t);
    ar(make_nvp("version", o.mProtocolVersion));
}
} // namespace cereal

namespace stellar
{
std::string
Upgrades::UpgradeParameters::toJson() const
{
    std::ostringstream out;
    {
        cereal::JSONOutputArchive ar(out);
        cereal::save(ar, *this);
    }
    return out.str();
}

void
Upgrades::UpgradeParameters::fromJson(std::string const& s)
{
    std::istringstream in(s);
    {
        cereal::JSONInputArchive ar(in);
        cereal::load(ar, *this);
    }
}

Upgrades::Upgrades(UpgradeParameters const& params) : mParams(params)
{
}

void
Upgrades::setParameters(UpgradeParameters const& params, Config const& cfg)
{
    if (params.mProtocolVersion &&
        *params.mProtocolVersion > cfg.LEDGER_PROTOCOL_VERSION)
    {
        throw std::invalid_argument(fmt::format(
            "Protocol version error: supported is up to {}, passed is {}",
            cfg.LEDGER_PROTOCOL_VERSION, *params.mProtocolVersion));
    }
    mParams = params;
}

Upgrades::UpgradeParameters const&
Upgrades::getParameters() const
{
    return mParams;
}

std::vector<LedgerUpgrade>
Upgrades::createUpgradesFor(LedgerHeader const &header, Application &app) const
{
    auto result = std::vector<LedgerUpgrade>{};
    auto config = app.getConfig();
    if (header.txExpirationPeriod != config.TX_EXPIRATION_PERIOD)
    {
        result.emplace_back(LedgerUpgradeType::TX_EXPIRATION_PERIOD);
        result.back().newTxExpirationPeriod() =
                config.TX_EXPIRATION_PERIOD;
    }

    if (header.maxTxSetSize != config.DESIRED_MAX_TX_PER_LEDGER)
    {
        result.emplace_back(LedgerUpgradeType::MAX_TX_SET_SIZE);
        result.back().newMaxTxSetSize() =
                config.DESIRED_MAX_TX_PER_LEDGER;
    }

    xdr::xvector<ExternalSystemIDGeneratorType> generators;
    auto availableGenerators = app.getAvailableExternalSystemGenerator();
    copy(availableGenerators.begin(), availableGenerators.end(), back_inserter(generators));
    sort(generators.begin(), generators.end());
    if (header.externalSystemIDGenerators != generators)
    {
        result.emplace_back(LedgerUpgradeType::EXTERNAL_SYSTEM_ID_GENERATOR);
        result.back().newExternalSystemIDGenerators() = generators;
    }

    if (mParams.mProtocolVersion && timeForUpgrade(header.scpValue.closeTime)
         && (header.ledgerVersion != *mParams.mProtocolVersion))
    {
        result.emplace_back(LedgerUpgradeType::VERSION);
        result.back().newLedgerVersion() = *mParams.mProtocolVersion;
    }

    return result;
}

void
Upgrades::applyTo(LedgerUpgrade const& upgrade, LedgerHeader& header)
{
    std::string logValue;
    switch (upgrade.type())
    {
    case LedgerUpgradeType::VERSION:
    {
        header.ledgerVersion = upgrade.newLedgerVersion();
        logValue = xdr::xdr_traits<LedgerVersion>::enum_name(LedgerVersion(upgrade.newLedgerVersion()));
        break;
    }
    case LedgerUpgradeType::MAX_TX_SET_SIZE:
    {
        header.maxTxSetSize = upgrade.newMaxTxSetSize();
        logValue = std::to_string(upgrade.newMaxTxSetSize());
        break;
    }
    case LedgerUpgradeType::TX_EXPIRATION_PERIOD:
    {
        header.txExpirationPeriod = upgrade.newTxExpirationPeriod();
        logValue = std::to_string(upgrade.newTxExpirationPeriod());
        break;
    }
    case LedgerUpgradeType::EXTERNAL_SYSTEM_ID_GENERATOR:
    {
        header.externalSystemIDGenerators = upgrade.newExternalSystemIDGenerators();
        break;
    }
    default:
    {
        std::string s;
        s = "Unknown upgrade type: ";
        s += std::to_string(static_cast<int32_t >(upgrade.type()));
        throw std::runtime_error(s);
    }
    }

    CLOG(INFO, "Ledger") << "Applied ledger upgrade. Type:" << xdr::xdr_traits<LedgerUpgradeType>::enum_name(upgrade.type())
                                                            << ". Value:" << logValue;
}

std::string
Upgrades::toString(LedgerUpgrade const& upgrade)
{
    switch (upgrade.type())
    {
    case LedgerUpgradeType::VERSION:
        return fmt::format("VERSION={0}", upgrade.newLedgerVersion());
    case LedgerUpgradeType::MAX_TX_SET_SIZE:
        return fmt::format("MAX_TX_SET_SIZE={0}", upgrade.newMaxTxSetSize());
    case LedgerUpgradeType::TX_EXPIRATION_PERIOD:
        return fmt::format("TX_EXPIRATION_PERIOD={0}", upgrade.newTxExpirationPeriod());
    case LedgerUpgradeType::EXTERNAL_SYSTEM_ID_GENERATOR:
        return fmt::format("EXTERNAL_SYSTEM_ID_GENERATOR={0}", upgrade.newExternalSystemIDGenerators().size());
    default:
        return "<unsupported>";
    }
}

std::string
Upgrades::toString() const
{
    fmt::MemoryWriter r;

    auto appendInfo = [&](std::string const& s, optional<uint32> const& o) {
        if (o)
        {
            if (!r.size())
            {
                r << "upgradetime="
                  << VirtualClock::pointToISOString(mParams.mUpgradeTime);
            }
            r << ", " << s << "=" << *o;
        }
    };
    appendInfo("protocol_version", mParams.mProtocolVersion);

    return r.str();
}

Upgrades::UpgradeParameters
Upgrades::removeUpgrades(std::vector<UpgradeType>::const_iterator beginUpdates,
                         std::vector<UpgradeType>::const_iterator endUpdates,
                         bool& updated)
{
    updated = false;
    UpgradeParameters res = mParams;

    auto resetParam = [&](optional<uint32>& o, uint32 v) {
        if (o && *o == v)
        {
            o.reset();
            updated = true;
        }
    };

    for (auto it = beginUpdates; it != endUpdates; it++)
    {
        auto& u = *it;
        LedgerUpgrade lu;
        try
        {
            xdr::xdr_from_opaque(u, lu);
        }
        catch (xdr::xdr_runtime_error&)
        {
            continue;
        }
        switch (lu.type())
        {
            case LedgerUpgradeType::VERSION:
            resetParam(res.mProtocolVersion, lu.newLedgerVersion());
            break;
        default:
            // skip unknown
            break;
        }
    }
    return res;
}

bool
Upgrades::isValid(UpgradeType const &upgrade, LedgerUpgradeType &upgradeType, bool nomination,
                  LedgerHeader const &header, Application &app) const
{
    LedgerUpgrade lupgrade;

    try
    {
        xdr::xdr_from_opaque(upgrade, lupgrade);
    }
    catch (xdr::xdr_runtime_error&)
    {
        return false;
    }

    auto config = app.getConfig();
    bool res = true;
    switch (lupgrade.type())
    {
    case LedgerUpgradeType::VERSION:
    {
        uint32 newVersion = lupgrade.newLedgerVersion();
        if (nomination && !timeForUpgrade(header.scpValue.closeTime))
        {
            return false;
        }

        if (nomination)
        {
            res = mParams.mProtocolVersion &&
                  (newVersion == *mParams.mProtocolVersion);
        }
        // only allow upgrades to a supported version of the protocol
        res = res && (newVersion <= config.LEDGER_PROTOCOL_VERSION);
        // and enforce versions to be strictly monotonic
        res = res && (newVersion > header.ledgerVersion);
    }
    break;
    case LedgerUpgradeType::MAX_TX_SET_SIZE:
    {
        // allow max to be within 30% of the config value
        uint32 newMax = lupgrade.newMaxTxSetSize();
        res = (newMax >= config.DESIRED_MAX_TX_PER_LEDGER * 7 / 10) &&
              (newMax <= config.DESIRED_MAX_TX_PER_LEDGER * 13 / 10);
    }
    break;
    case LedgerUpgradeType::EXTERNAL_SYSTEM_ID_GENERATOR:
    {
        auto newGenerators = lupgrade.newExternalSystemIDGenerators();
        res = app.areAllExternalSystemGeneratorsAvailable(newGenerators);
    }
    break;
    case LedgerUpgradeType::TX_EXPIRATION_PERIOD:
    {
        auto newPeriod = lupgrade.newTxExpirationPeriod();
        res = (newPeriod == config.TX_EXPIRATION_PERIOD);
    }
    break;
    default:
        res = false;
    }

    if (res)
    {
        upgradeType = lupgrade.type();
    }
    return res;
}

bool
Upgrades::timeForUpgrade(uint64_t time) const
{
    return mParams.mUpgradeTime <= VirtualClock::from_time_t(time);
}
}
