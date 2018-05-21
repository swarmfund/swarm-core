// Copyright 2017 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#ifndef EXTERNAL_SYSTEM_ID_GENERATOR_H
#define EXTERNAL_SYSTEM_ID_GENERATOR_H
#include "ledger/ExternalSystemAccountID.h"

namespace stellar {

    const int32 BitcoinExternalSystemType = 1;
    const int32 EthereumExternalSystemType = 2;

    class Database;
    class Application;
    class LedgerDelta;

    class Generator
    {
    protected:
        Database& mDb;
        Application& mApp;

        virtual ExternalSystemAccountIDFrame::pointer generateNewID(AccountID const& accountID, uint64_t id) = 0;

    public:
        Generator(Application& app, Database& db);
        virtual ~Generator();

        // tryGenerateNewID - generates new id, if it does not exist for specified account in external system 
        ExternalSystemAccountIDFrame::pointer tryGenerateNewID(AccountID const& accountID, uint64_t id);
        virtual int32 getExternalSystemType() const = 0;
    };
}

#endif //EXTERNAL_SYSTEM_ID_GENERATOR_H
