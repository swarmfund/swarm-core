
#pragma once

#include "ledger/AccountFrame.h"
#include "util/types.h"

namespace stellar
{

struct PolicyDetails
{
  private:
    bool isEmpty;

  public:
    const std::vector<std::string> mResourceIDs;
    const AccountID mPolicyOwner;
    std::string mAction;
    AccountID mInitiator;

    PolicyDetails() : isEmpty(true)
    {
    }
    PolicyDetails(const std::vector<std::string>& resourceIDs,
                  const AccountID &policyOwner,
                  const std::string& action,
                  const AccountID& initiator)
        : mResourceIDs(resourceIDs)
        , mPolicyOwner(policyOwner)
        , mAction(action)
        , mInitiator(initiator)
        , isEmpty(false)
    {
    }

    bool
    empty() const
    {
        return isEmpty;
    }
};

} // namespace stellar