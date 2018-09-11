#include "ledger/FeeFrame.h"
#include "database/Database.h"
#include "crypto/SecretKey.h"
#include "crypto/SHA.h"
#include "LedgerDelta.h"
#include "util/types.h"

using namespace std;
using namespace soci;

namespace stellar
{

FeeFrame::FeeFrame() : EntryFrame(LedgerEntryType::FEE), mFee(mEntry.data.feeState())
{
}

FeeFrame::FeeFrame(LedgerEntry const& from)
: EntryFrame(from), mFee(mEntry.data.feeState())
{
}

FeeFrame::FeeFrame(FeeFrame const& from) : FeeFrame(from.mEntry)
{
}

FeeFrame& FeeFrame::operator=(FeeFrame const& other)
{
    if (&other != this)
    {
        mFee = other.mFee;
        mKey = other.mKey;
        mKeyCalculated = other.mKeyCalculated;
    }
    return *this;
}

FeeFrame::pointer FeeFrame::create(FeeType feeType, int64_t fixedFee,
    int64_t percentFee, AssetCode asset, AccountID* accountID,
    AccountType* accountType, int64_t subtype, int64_t lowerBound, int64_t upperBound)
{
    LedgerEntry le;
    le.data.type(LedgerEntryType::FEE);
    FeeEntry& entry = le.data.feeState();
    entry.fixedFee = fixedFee;
    entry.percentFee = percentFee;
    entry.feeType = feeType;
    entry.asset = asset;
    entry.subtype = subtype;
    if (accountID)
        entry.accountID.activate() = *accountID;

    if (accountType)
        entry.accountType.activate() = *accountType;

    entry.lowerBound = lowerBound;
    entry.upperBound = upperBound;

    entry.hash = calcHash(feeType, asset, accountID, accountType, subtype);
    return std::make_shared<FeeFrame>(le);
}

bool FeeFrame::isInRange(int64_t a, int64_t b, int64_t point)
{
    return a <= point && point <= b;
}

int64_t FeeFrame::calculatePercentFee(int64_t amount, bool roundUp)
{
    if (mFee.percentFee == 0)
        return 0;
    auto rounding = roundUp ? ROUND_UP : ROUND_DOWN;
    return bigDivide(amount, mFee.percentFee, 100 * ONE, rounding);
}

bool FeeFrame::calculatePercentFee(const uint64_t amount, uint64_t& result,
                                   const Rounding rounding) const
{
    result = 0;
    if (mFee.percentFee == 0)
    {
        return true;
    }

    return bigDivide(result, amount, mFee.percentFee, 100 * ONE, rounding);
}

int64_t FeeFrame::calculatePercentFeeForPeriod(int64_t amount, int64_t periodPassed, int64_t basePeriod)
{
    if (mFee.percentFee == 0
        || periodPassed == 0
        || basePeriod == 0
        || amount == 0)
    {
        return 0;
    }

    int64_t percentFeeForFullPeriod = calculatePercentFee(amount);
    return bigDivide(percentFeeForFullPeriod, periodPassed, basePeriod, ROUND_UP);
}

    
bool FeeFrame::isCrossAssetFee() const
{
    if (mFee.ext.v() != LedgerVersion::CROSS_ASSET_FEE) {
        return false;
    }

    return !(mFee.asset == mFee.ext.feeAsset());
}

AssetCode FeeFrame::getFeeAsset() const
{
    if (mFee.ext.v() != LedgerVersion::CROSS_ASSET_FEE) {
        return mFee.asset;
    }

    return mFee.ext.feeAsset();
}

bool
FeeFrame::isValid(FeeEntry const& oe)
{
    auto res = oe.fixedFee >= 0
               && oe.percentFee >= 0
               && oe.percentFee <= 100 * ONE
               && isFeeTypeValid(oe.feeType)
               && oe.lowerBound <= oe.upperBound;
    return res;
}

Hash
FeeFrame::calcHash(FeeType feeType, AssetCode asset, AccountID* accountID, AccountType* accountType, int64_t subtype)
{
    std::string data = "";

    char buff[100];
    snprintf(buff, sizeof(buff), "type:%i", feeType);
    std::string buffAsStdStr = buff;
    data += buffAsStdStr;

    std::string rawAsset = asset;
    snprintf(buff, sizeof(buff), "asset:%s", rawAsset.c_str());
    buffAsStdStr = buff;
    data += buffAsStdStr;

    snprintf(buff, sizeof(buff), "subtype:%s", std::to_string(subtype).c_str());
    buffAsStdStr = buff;
    data += buffAsStdStr;

    if (accountID) {
        std::string actIDStrKey = PubKeyUtils::toStrKey(*accountID);
        snprintf(buff, sizeof(buff), "accountID:%s", actIDStrKey.c_str());
        buffAsStdStr = buff;
        data += buffAsStdStr;
    }
    if (accountType) {
        snprintf(buff, sizeof(buff), "accountType:%i", *accountType);
        buffAsStdStr = buff;
        data += buffAsStdStr;
    }

    return Hash(sha256(data));
}

bool
FeeFrame::isValid() const
{
    return isValid(mFee);
}

void FeeFrame::checkFeeType(FeeEntry const& feeEntry, FeeType feeType)
{
    if (feeEntry.feeType != feeType)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected fee type. Expected: "
                                               << xdr::xdr_traits<FeeType>::enum_name(feeType)
                                               << " but was: "
                                               << xdr::xdr_traits<FeeType>::enum_name(feeEntry.feeType);
        throw std::runtime_error("Unexpected fee type");
    }
}

}
