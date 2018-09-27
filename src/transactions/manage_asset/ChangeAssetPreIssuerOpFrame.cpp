// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <lib/xdrpp/xdrpp/printer.h>
#include <transactions/review_request/ReviewIssuanceCreationRequestOpFrame.h>
#include <main/Application.h>
#include <transactions/review_request/ReviewRequestHelper.h>
#include "ChangeAssetPreIssuerOpFrame.h"
#include "ledger/AccountHelper.h"
#include "ledger/AssetHelperLegacy.h"

namespace stellar
{
using namespace std;
using xdr::operator==;

ChangeAssetPreIssuerOpFrame::ChangeAssetPreIssuerOpFrame(Operation const& op,
                                                         OperationResult& res,
                                                         TransactionFrame&
                                                         parentTx)
    : ManageAssetOpFrame(op, res, parentTx)
    , mAssetChangePreissuedSigner(mManageAsset.request.changePreissuedSigner())
{
}

SourceDetails ChangeAssetPreIssuerOpFrame::getSourceAccountDetails(
    std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
    int32_t ledgerVersion) const
{
    throw std::
        runtime_error("Get source details for ChangeAssetPreIssuerOpFrame is not implemented");
}

SourceDetails ChangeAssetPreIssuerOpFrame::getSourceAccountDetails(
    unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
    const int32_t ledgerVersion, Database& db) const
{
    const auto assetFrame = AssetHelperLegacy::Instance()->
        loadAsset(mAssetChangePreissuedSigner.code, db);
    vector<PublicKey> signers;
    if (!!assetFrame)
    {
        signers.push_back(assetFrame->getPreIssuedAssetSigner());
    }

    if (ledgerVersion >= int32_t(LedgerVersion::ASSET_PREISSUER_MIGRATION) && ledgerVersion < int32_t(LedgerVersion::ASSET_PREISSUER_MIGRATED))
    {
        signers.push_back(assetFrame->getOwner());
    }

    return SourceDetails({AccountType::MASTER, AccountType::SYNDICATE}, 1, 0, 0, signers);
}

bool ChangeAssetPreIssuerOpFrame::doApply(Application& app, LedgerDelta& delta,
                                          LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();
    auto assetFrame = AssetHelperLegacy::Instance()->
        loadAsset(mAssetChangePreissuedSigner.code, db, &delta);
    if (!assetFrame)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) <<
            "Unexpected state: asset must exists for ChangeAssetPreIssuer. Code: "
            << mAssetChangePreissuedSigner.code;
        throw std::
            runtime_error("Unexpected state: asset must exists for ChangeAssetPreIssuer");
    }

    auto& assetEntry = assetFrame->getAsset();
    assetEntry.preissuedAssetSigner = mAssetChangePreissuedSigner.accountID;
    AssetHelperLegacy::Instance()->storeChange(delta, db, assetFrame->mEntry);
    innerResult().code(ManageAssetResultCode::SUCCESS);
    innerResult().success().requestID = 0;
    innerResult().success().fulfilled = true;
    return true;
}

bool ChangeAssetPreIssuerOpFrame::doCheckValid(Application& app)
{
    return true;
}

string ChangeAssetPreIssuerOpFrame::getAssetCode() const
{
    return mAssetChangePreissuedSigner.code;
}
}
