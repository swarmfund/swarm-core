#include "LimitsV2Frame.h"


using namespace std;
using namespace soci;

namespace stellar
{
    LimitsV2Frame::LimitsV2Frame() : EntryFrame(LedgerEntryType::LIMITS_V2), mLimitsV2(mEntry.data.limitsV2()){}

    LimitsV2Frame::LimitsV2Frame(LedgerEntry const& from) : EntryFrame(from), mLimitsV2(mEntry.data.limitsV2()) {}

    LimitsV2Frame::LimitsV2Frame(LimitsV2Frame const& from) : LimitsV2Frame(from.mEntry){}

    LimitsV2Frame& LimitsV2Frame::operator=(LimitsV2Frame const& other)
    {
        if (&other != this)
        {
            mLimitsV2 = other.mLimitsV2;
            mKey = other.mKey;
            mKeyCalculated = other.mKeyCalculated;
        }
        return *this;
    }

    bool
    LimitsV2Frame::isValid(LimitsV2Entry const& limitsV2)
    {
        if (limitsV2.dailyOut > limitsV2.weeklyOut)
            return false;
        if (limitsV2.weeklyOut > limitsV2.monthlyOut)
            return false;
        return limitsV2.monthlyOut <= limitsV2.annualOut;
    }

    bool LimitsV2Frame::isValid() const
    {
        return isValid(mLimitsV2);
    }

    void LimitsV2Frame::changeLimits(ManageLimitsOp const& manageLimitsOp)
    {
        mLimitsV2.dailyOut = manageLimitsOp.dailyOut;
        mLimitsV2.weeklyOut = manageLimitsOp.weeklyOut;
        mLimitsV2.monthlyOut = manageLimitsOp.monthlyOut;
        mLimitsV2.annualOut = manageLimitsOp.annualOut;
    }

    LimitsV2Frame::pointer
    LimitsV2Frame::createNew(uint64_t id, ManageLimitsOp const& manageLimitsOp)
    {
        LedgerEntry le;
        le.data.type(LedgerEntryType::LIMITS_V2);
        LimitsV2Entry& entry = le.data.limitsV2();

        entry.id = id;
        entry.accountType = manageLimitsOp.details.updateLimitsDetails().accountType;
        entry.accountID = manageLimitsOp.details.updateLimitsDetails().accountID;
        entry.statsOpType = manageLimitsOp.details.updateLimitsDetails().statsOpType;
        entry.assetCode = manageLimitsOp.details.updateLimitsDetails().assetCode;
        entry.isConvertNeeded = manageLimitsOp.details.updateLimitsDetails().isConvertNeeded;

        auto limitsV2Frame = std::make_shared<LimitsV2Frame>(le);
        limitsV2Frame->changeLimits(manageLimitsOp);

        return limitsV2Frame;
    }
}
