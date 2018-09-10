// Copyright 2017 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "Generator.h"
#include "ledger/ExternalSystemAccountIDHelperLegacy.h"

namespace stellar {

Generator::Generator(Application& app, Database& db) : mDb(db), mApp(app)
{
}

Generator::~Generator() = default;

ExternalSystemAccountIDFrame::pointer Generator::tryGenerateNewID(
    AccountID const& accountID, const uint64_t id)
{
	auto externalSystemAccountIDHelper = ExternalSystemAccountIDHelperLegacy::Instance();
    if (externalSystemAccountIDHelper->exists(mDb, accountID, getExternalSystemType()))
    {
        return nullptr;
    }

    return generateNewID(accountID, id);
}
}
