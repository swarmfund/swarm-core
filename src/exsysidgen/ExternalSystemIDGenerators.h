// Copyright 2017 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0
#pragma once

#ifndef EXTERNAL_SYSTEM_ID_GENERATORS_H
#define EXTERNAL_SYSTEM_ID_GENERATORS_H
#include "ledger/ExternalSystemAccountID.h"
#include "main/Application.h"
#include <vector>
#include <memory>


namespace stellar
{

    class Generator;
    class LedgerDelta;
    class Database;

    class ExternalSystemIDGenerators
    {

        LedgerDelta& mDelta;

        std::shared_ptr<Generator> getGeneratorForType(Application& app, Database& db, ExternalSystemIDGeneratorType types) const;

    public:
        ExternalSystemIDGenerators(Application& app, LedgerDelta& delta, Database& db);
        ~ExternalSystemIDGenerators();

        std::vector<ExternalSystemAccountIDFrame::pointer> generateNewIDs(AccountID const& accountID);

    private:
        std::vector<std::shared_ptr<Generator>> mGenerators;
    };
}



#endif //EXTERNAL_SYSTEM_ID_GENERATORS_H
