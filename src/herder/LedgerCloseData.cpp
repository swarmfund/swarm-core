#include "LedgerCloseData.h"
#include "main/Application.h"
#include "crypto/Hex.h"
#include <overlay/OverlayManager.h>
#include <xdrpp/marshal.h>
#include "util/Logging.h"

using namespace std;

namespace stellar
{

LedgerCloseData::LedgerCloseData(uint32_t ledgerSeq, TxSetFramePtr txSet,
                                 StellarValue const& v)
    : mLedgerSeq(ledgerSeq), mTxSet(txSet), mValue(v)
{
    Value x;
    Value y(x.begin(), x.end());

    using xdr::operator==;
    assert(txSet->getContentsHash() == mValue.txSetHash);
}

std::string
stellarValueToString(StellarValue const& sv)
{
    std::stringstream res;

    res << "[ "
        << " txH: " << hexAbbrev(sv.txSetHash) << ", ct: " << sv.closeTime
        << ", upgrades: [";
    for (auto const& upgrade : sv.upgrades)
    {
        if (upgrade.empty())
        {
            // should not happen as this is not valid
            res << "<empty>";
        }
        else
        {
            try
            {
                LedgerUpgrade lupgrade;
                xdr::xdr_from_opaque(upgrade, lupgrade);
                switch (lupgrade.type())
                {
                case LedgerUpgradeType::VERSION:
                    res << "VERSION=" << lupgrade.newLedgerVersion();
                    break;
                case LedgerUpgradeType::MAX_TX_SET_SIZE:
                    res << "MAX_TX_SET_SIZE=" << lupgrade.newMaxTxSetSize();
                    break;
                case LedgerUpgradeType::ISSUANCE_KEYS:
                    res << "ISSUANCE_KEYS=" << lupgrade.newIssuanceKeys().size();
                    break;
                case LedgerUpgradeType::TX_EXPIRATION_PERIOD:
                    res << "TX_EXPIRATION_PERIOD=" << lupgrade.newTxExpirationPeriod();
                    break;
                default:
                    res << "<unsupported>";
                }
            }
            catch (std::exception&)
            {
                res << "<unknown>";
            }
        }
        res << ", ";
    }
    res << " ] ]";

    return res.str();
}
}
