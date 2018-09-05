
#pragma once

#include "ledger/AccountFrame.h"
#include "util/types.h"

namespace stellar
{

struct PolicyDetails
{
  private:
    bool isEmpty;
    const std::string mResourceID;
    const std::string mAction;

  public:
    PolicyDetails() : isEmpty(true)
    {
    }
    PolicyDetails(const std::string& resourceID, const std::string& action)
        : mResourceID(resourceID), mAction(action), isEmpty(false)
    {
    }

    bool
    empty() const
    {
        return isEmpty;
    }

    const std::string&
    getResourceID() const
    {
        return mResourceID;
    }

    const std::string&
    getAction() const
    {
        return mAction;
    }
};

} // namespace stellar