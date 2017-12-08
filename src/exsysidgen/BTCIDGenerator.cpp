// Copyright 2017 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "BTCIDGenerator.h"
#include "main/Application.h"

namespace stellar {
ExternalSystemAccountIDFrame::pointer BTCIDGenerator::generateNewID(
    AccountID const& accountID, const uint64_t id)
{
    auto addresses = mApp.getBTCAddresses();
    if (addresses.size() <= id)
    {
        throw std::runtime_error("Requested id for btc address generation exceeds number of available addresses");
    }

    return ExternalSystemAccountIDFrame::createNew(accountID, getExternalSystemType(), addresses[id]);
}
}
