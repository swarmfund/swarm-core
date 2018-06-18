//
// Created by artem on 29.05.18.
//
#pragma once

#include "ledger/EntryFrame.h"

namespace  soci
{
    class session;
}

namespace stellar
{
class StatementContext;


class LimitsV2Frame : public EntryFrame
{
    LimitsV2Entry& mLimitsV2;

    LimitsV2Frame(LimitsV2Frame const& from);

public:
    typedef std::shared_ptr<LimitsV2Frame> pointer;

    LimitsV2Frame();
    LimitsV2Frame(LedgerEntry const& from);

    LimitsV2Frame& operator=(LimitsV2Frame const& other);

    EntryFrame::pointer
    copy() const override
    {
        return EntryFrame::pointer(new LimitsV2Frame(*this));
    }

    LimitsV2Entry const&
    getLimits() const
    {
        return mLimitsV2;
    }
    uint64_t getDailyOut()
    {
        return mLimitsV2.dailyOut;
    }

    uint64_t getWeeklyOut() {
        return mLimitsV2.weeklyOut;
    }

    uint64_t getMonthlyOut() {
        return mLimitsV2.monthlyOut;
    }

    uint64_t getAnnualOut() {
        return mLimitsV2.annualOut;
    }

    uint64_t const&
    getID() const
    {
        return mLimitsV2.id;
    }

    bool const&
    getConvertNeeded() const
    {
        return mLimitsV2.isConvertNeeded;
    }

    AssetCode const&
    getAsset() const
    {
        return mLimitsV2.assetCode;
    }

    static bool isValid(LimitsV2Entry const& limitsV2);
    bool isValid() const;

    static LimitsV2Frame::pointer createNew(uint64_t id, ManageLimitsOp const& manageLimitsOp);

    void changeLimits(ManageLimitsOp const& manageLimitsOp);
};

}
