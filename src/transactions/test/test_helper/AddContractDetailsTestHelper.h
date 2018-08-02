#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "overlay/StellarXDR.h"
#include "util/optional.h"
#include "TxHelper.h"

namespace stellar
{
namespace txtest
{

class AddContractDetailsTestHelper : TxHelper
{
public:
    explicit AddContractDetailsTestHelper(TestManager::pointer testManager);

    TransactionFramePtr createAddContractDetailsTx(Account &source, uint64_t& contractID, longstring& details);

    void applyAddContractDetailsTx(Account &source, uint64_t& contractID, longstring details,
                                   AddContractDetailsResultCode expectedResult = AddContractDetailsResultCode::SUCCESS);
};
}
}