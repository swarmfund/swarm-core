
#include "SetIdentityPolicyOpFrame.h"
#include "database/Database.h"
#include "ledger/IdentityPolicyHelper.h"
#include "ledger/LedgerDelta.h"
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"

#include <algorithm>
#include <regex>
#include <tuple>

namespace stellar
{

using namespace std;
using xdr::operator==;

SetIdentityPolicyOpFrame::SetIdentityPolicyOpFrame(const Operation& op,
                                                   OperationResult& res,
                                                   TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mSetIdentityPolicy(mOperation.body.setIdentityPolicyOp())
{
}

bool
SetIdentityPolicyOpFrame::doApply(Application& app, LedgerDelta& delta,
                                  LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();
    innerResult().code(SetIdentityPolicyResultCode::SUCCESS);

    return trySetIdentityPolicy(db, delta);
}

bool
SetIdentityPolicyOpFrame::doCheckValid(Application& app)
{
    return checkResourceValue();
}

bool
SetIdentityPolicyOpFrame::trySetIdentityPolicy(Database& db, LedgerDelta& delta)
{
    auto identityPolicyHelper = IdentityPolicyHelper::Instance();
    auto identityPolicyFrame = identityPolicyHelper->loadIdentityPolicy(
        mSetIdentityPolicy.id, db, &delta);

    // delete
    if (mSetIdentityPolicy.isDelete)
    {
        if (!identityPolicyFrame)
        {
            innerResult().code(SetIdentityPolicyResultCode::NOT_FOUND);
            return false;
        }

        EntryHelperProvider::storeDeleteEntry(delta, db,
                                              identityPolicyFrame->getKey());
        return true;
    }

    // update
    if (identityPolicyFrame)
    {
        auto& entityType = identityPolicyFrame->getIdentityPolicy();
        entityType.id = mSetIdentityPolicy.id;
        EntryHelperProvider::storeChangeEntry(delta, db,
                                              identityPolicyFrame->mEntry);
        return true;
    }

    // create
    LedgerEntry le;
    le.data.type(LedgerEntryType::IDENTITY_POLICY);
    le.data.identityPolicy().id = mSetIdentityPolicy.id;
    le.data.identityPolicy().priority = mSetIdentityPolicy.priority;
    le.data.identityPolicy().resource = mSetIdentityPolicy.resource;
    le.data.identityPolicy().effect = mSetIdentityPolicy.effect;

    identityPolicyFrame = make_shared<IdentityPolicyFrame>(le);
    EntryHelperProvider::storeAddEntry(delta, db, identityPolicyFrame->mEntry);

    return true;
}

SourceDetails
SetIdentityPolicyOpFrame::getSourceAccountDetails(
    std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
    int32_t ledgerVersion) const
{
    return SourceDetails(
        {
            AccountType::MASTER,
        },
        mSourceAccount->getMediumThreshold(),
        static_cast<int32_t>(SignerType::IDENTITY_POLICY_MANAGER));
}
std::unordered_map<AccountID, CounterpartyDetails>
SetIdentityPolicyOpFrame::getCounterpartyDetails(Database& db,
                                                 LedgerDelta* delta) const
{
    return {};
}

bool
SetIdentityPolicyOpFrame::checkResourceValue()
{
    auto fields = getResourceFields();

    auto resourceNameIter =
        std::find(allowedResources.cbegin(), allowedResources.cend(),
                  std::get<0>(fields));
    if (resourceNameIter == allowedResources.cend())
    {
        return false;
    }

    auto resourcePropertiesKeyIter =
        allowedResourceProperties.find(*resourceNameIter);
    if (resourcePropertiesKeyIter == allowedResourceProperties.cend())
    {
        return false;
    }

    auto resourcePropIter = std::find(
        resourcePropertiesKeyIter->second.cbegin(),
        resourcePropertiesKeyIter->second.cend(), std::get<1>(fields));
    if (resourcePropIter == resourcePropertiesKeyIter->second.cend())
    {
        return false;
    }

    return true;
}

std::tuple<std::string, std::string, std::string>
SetIdentityPolicyOpFrame::getResourceFields()
{
    const std::regex resourceRegEx{R"(^resource_type:(.+):(*+):(*+)$)"};
    auto fields = make_tuple(
        std::regex_replace(mSetIdentityPolicy.resource, resourceRegEx, "$1"),
        std::regex_replace(mSetIdentityPolicy.resource, resourceRegEx, "$2"),
        std::regex_replace(mSetIdentityPolicy.resource, resourceRegEx, "$3"));

    return fields;
}

} // namespace stellar