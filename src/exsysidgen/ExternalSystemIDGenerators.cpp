
// Copyright 2017 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ExternalSystemIDGenerators.h"
#include "BTCIDGenerator.h"
#include "ETHIDGenerator.h"
#include "ledger/LedgerDelta.h"
#include "ledger/LedgerHeaderFrame.h"

namespace stellar
{
std::shared_ptr<Generator> ExternalSystemIDGenerators::getGeneratorForType(
    Application& app, Database& db,
    const ExternalSystemIDGeneratorType type) const
{
    switch (type)
    {
    case ExternalSystemIDGeneratorType::BITCOIN_BASIC:
        return std::make_shared<BTCIDGenerator>(app, db, app.getBTCAddressRoot());
    case ExternalSystemIDGeneratorType::ETHEREUM_BASIC:
        return std::make_shared<ETHIDGenerator>(app, db, app.getETHAddressRoot());
    default:
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) <<
            "Unexpected external system generator type: " << xdr::xdr_traits<
                ExternalSystemIDGeneratorType>::enum_name(type);
        throw std::runtime_error("Unexpected external system generator type");
    }
    }
}

ExternalSystemIDGenerators::ExternalSystemIDGenerators(Application& app,
                                                       LedgerDelta& delta,
                                                       Database&
                                                       db): mDelta(delta)
{
    for (auto generatorType : mDelta.getHeaderFrame().getHeader().
                                     externalSystemIDGenerators)
    {
        mGenerators.push_back(getGeneratorForType(app, db, generatorType));
    }
}

ExternalSystemIDGenerators::~ExternalSystemIDGenerators() = default;

std::vector<ExternalSystemAccountIDFrame::pointer> ExternalSystemIDGenerators::generateNewIDs(AccountID const& accountID)
{
    const auto id = mDelta.getHeaderFrame().generateID(LedgerEntryType::EXTERNAL_SYSTEM_ACCOUNT_ID);
    std::vector<ExternalSystemAccountIDFrame::pointer> results;
    for (auto& generator : mGenerators)
    {
        const auto result = generator->tryGenerateNewID(accountID, id);
        if (!result)
        {
            continue;
        }
        results.push_back(result);
    }

    return results;
}
}
