// Copyright 2017 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ETHIDGenerator.h"
#include "main/Application.h"

namespace stellar {
ExternalSystemAccountIDFrame::pointer ETHIDGenerator::generateNewID(
    AccountID const& accountID, const uint64_t id)
{
    auto addresses = mApp.getETHAddresses();
    if (addresses.size() <= id)
    {
        throw std::runtime_error("Requested id for eth address generation exceeds number of available addresses");
    }

    return ExternalSystemAccountIDFrame::createNew(accountID, getExternalSystemType(), addresses[id]);
}
}
