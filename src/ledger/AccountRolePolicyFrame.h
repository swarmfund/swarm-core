
#pragma once

#include "ledger/AccountFrame.h"
#include "ledger/EntryFrame.h"
#include "util/types.h"

#include <functional>
#include <stdio.h>
#include <unordered_map>

namespace stellar
{
class AccountRolePolicyFrame : public EntryFrame
{

    AccountRolePolicyEntry& mAccountRolePolicyEntry;
    AccountRolePolicyFrame(AccountRolePolicyFrame const& from);

  public:
    AccountRolePolicyFrame();
    explicit AccountRolePolicyFrame(LedgerEntry const& from);

    using pointer = std::shared_ptr<AccountRolePolicyFrame>;

    AccountRolePolicyEntry&
    getIdentityPolicy() const
    {
        return mAccountRolePolicyEntry;
    }

    uint64_t
    getID() const
    {
        return mAccountRolePolicyEntry.accountRolePolicyID;
    }

    std::string
    getResource() const
    {
        return mAccountRolePolicyEntry.resource;
    }

    std::string
    getAction() const
    {
        return mAccountRolePolicyEntry.action;
    }

    AccountRolePolicyEffect
    getEffect() const
    {
        return mAccountRolePolicyEntry.effect;
    }

    AccountID
    getOwnerID() const
    {
        return mAccountRolePolicyEntry.ownerID;
    }

    EntryFrame::pointer copy() const override;

    static bool isEffectValid(AccountRolePolicyEffect const effect);

    static void ensureValid(AccountRolePolicyEntry const& oeEntry);
    void ensureValid() const;
};

} // namespace stellar
