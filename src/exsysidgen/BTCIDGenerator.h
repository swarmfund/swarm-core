// Copyright 2017 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#ifndef EXTERNAL_SYSTEM_ID_BTC_GENERATOR_H
#define EXTERNAL_SYSTEM_ID_BTC_GENERATOR_H

#include "exsysidgen/Generator.h"

namespace stellar {

    class BTCIDGenerator : public Generator {
    public:
        BTCIDGenerator(Application& app, Database& db)
            : Generator(app, db)
        {
        }

        ExternalSystemAccountIDFrame::pointer generateNewID(
            AccountID const& accountID, uint64_t id) override;

        ExternalSystemType getExternalSystemType() const override
        {
            return ExternalSystemType::BITCOIN;
        }
    };
}

#endif //EXTERNAL_SYSTEM_ID_BTC_GENERATOR_H
