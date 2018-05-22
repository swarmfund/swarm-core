
#pragma once

#include "ledger/AccountFrame.h"
#include "ledger/EntryFrame.h"
#include "util/types.h"

#include <functional>
#include <stdio.h>
#include <unordered_map>

namespace stellar
{
class IdentityPolicyFrame : public EntryFrame
{

    IdentityPolicyEntry& mIdentityPolicyEntry;
    IdentityPolicyFrame(IdentityPolicyFrame const& from);

  public:
    IdentityPolicyFrame();
    explicit IdentityPolicyFrame(LedgerEntry const& from);

    using pointer = std::shared_ptr<IdentityPolicyFrame>;

    IdentityPolicyEntry&
    getIdentityPolicy() const
    {
        return mIdentityPolicyEntry;
    }

    uint64_t
    getID() const
    {
        return mIdentityPolicyEntry.id;
    }

    uint64_t
    getPriority() const
    {
        return mIdentityPolicyEntry.priority;
    }

    std::string
    getResource() const
    {
        return mIdentityPolicyEntry.resource;
    }

    std::string
    getAction() const
    {
        return mIdentityPolicyEntry.action;
    }

    Effect
    getEffect() const
    {
        return mIdentityPolicyEntry.effect;
    }

    AccountID
    getOwnerID() const
    {
        return mIdentityPolicyEntry.ownerID;
    }

    EntryFrame::pointer copy() const override;

    static bool isEffectValid(Effect const effect);

    static void ensureValid(IdentityPolicyEntry const& oeEntry);
    void ensureValid() const;
};

} // namespace stellar
