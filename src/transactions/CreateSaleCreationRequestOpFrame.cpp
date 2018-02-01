// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/CreateSaleCreationRequestOpFrame.h"
#include "database/Database.h"
#include "main/Application.h"
#include "medida/metrics_registry.h"
#include "ledger/LedgerDelta.h"
#include "ledger/AccountHelper.h"
#include "ledger/BalanceHelper.h"
#include "ledger/AssetHelper.h"
#include "ledger/ReviewableRequestFrame.h"
#include "xdrpp/printer.h"
#include "ledger/ReviewableRequestHelper.h"
#include "bucket/BucketApplicator.h"
#include "ledger/SaleFrame.h"
#include "ledger/AssetPairHelper.h"

namespace stellar
{
using xdr::operator==;


std::unordered_map<AccountID, CounterpartyDetails>
CreateSaleCreationRequestOpFrame::getCounterpartyDetails(
    Database& db, LedgerDelta* delta) const
{
    // source account is only counterparty
    return {};
}

SourceDetails CreateSaleCreationRequestOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                                                        int32_t ledgerVersion)
const
{
    return SourceDetails({
                             AccountType::SYNDICATE,
                         }, mSourceAccount->getHighThreshold(),
                         static_cast<int32_t>(SignerType::ASSET_MANAGER));
}

AssetFrame::pointer CreateSaleCreationRequestOpFrame::tryLoadBaseAssetOrRequest(
    SaleCreationRequest const& request,
    Database& db) const
{
    const auto assetFrame = AssetHelper::Instance()->loadAsset(request.baseAsset, getSourceID(), db);
    if (!!assetFrame)
    {
        return assetFrame;
    }

    auto assetCreationRequests = ReviewableRequestHelper::Instance()->loadRequests(getSourceID(), ReviewableRequestType::ASSET_CREATE, db);
    for (auto assetCreationRequestFrame : assetCreationRequests)
    {
        auto& assetCreationRequest = assetCreationRequestFrame->getRequestEntry().body.assetCreationRequest();
        if (assetCreationRequest.code == request.baseAsset)
        {
            return AssetFrame::create(assetCreationRequest, getSourceID());
        }
    }

    return nullptr;
}

std::string CreateSaleCreationRequestOpFrame::getReference(SaleCreationRequest const& request) const
{
    const auto hash = sha256(xdr_to_opaque(ReviewableRequestType::SALE, request.baseAsset));
    return binToHex(hash);
}

ReviewableRequestFrame::pointer CreateSaleCreationRequestOpFrame::
createNewUpdateRequest(Application& app, Database& db, LedgerDelta& delta, const time_t closedAt) const
{
    if (mCreateSaleCreationRequest.requestID != 0)
    {
        const auto requestFrame = ReviewableRequestHelper::Instance()->loadRequest(mCreateSaleCreationRequest.requestID, getSourceID(), db, &delta);
        if (!requestFrame)
        {
            return nullptr;
        }
    }

    auto const& sale = mCreateSaleCreationRequest.request;
    auto reference = getReference(sale);
    const auto referencePtr = xdr::pointer<string64>(new string64(reference));
    auto request = ReviewableRequestFrame::createNew(mCreateSaleCreationRequest.requestID, getSourceID(), app.getMasterID(),
        referencePtr, closedAt);
    auto& requestEntry = request->getRequestEntry();
    requestEntry.body.type(ReviewableRequestType::SALE);
    requestEntry.body.saleCreationRequest() = sale;
    request->recalculateHashRejectReason();
    return request;
}

bool CreateSaleCreationRequestOpFrame::isBaseAssetHasSufficientIssuance(
    const AssetFrame::pointer assetFrame)
{
    // TODO: fixme
    if (!assetFrame->isAvailableForIssuanceAmountSufficient(ONE))
    {
        innerResult().code(CreateSaleCreationRequestResultCode::INSUFFICIENT_PREISSUED);
        return false;
    }

    return true;    
}

bool CreateSaleCreationRequestOpFrame::areQuoteAssetsValid(Database& db, xdr::xvector<SaleCreationRequestQuoteAsset, 100> quoteAssets, AssetCode defaultQuoteAsset)
{
    if (!AssetHelper::Instance()->exists(db, defaultQuoteAsset))
    {
        return false;
    }

    for (auto const& quoteAsset : quoteAssets)
    {
        if (!AssetHelper::Instance()->exists(db, quoteAsset.quoteAsset))
        {
            return false;
        }

        if (defaultQuoteAsset == quoteAsset.quoteAsset)
            continue;

        const auto assetPair = AssetPairHelper::Instance()->tryLoadAssetPairForAssets(defaultQuoteAsset,
            quoteAsset.quoteAsset,
            db);
        if (!assetPair)
        {
            return false;
        }

    }

    return true;
}

bool CreateSaleCreationRequestOpFrame::isPriceValid(
    SaleCreationRequestQuoteAsset const& quoteAsset) const
{
    if (quoteAsset.price == 0)
    {
        return false;
    }

    uint64_t requiredBaseAsset;
    if (!SaleFrame::convertToBaseAmount(quoteAsset.price, mCreateSaleCreationRequest.request.hardCap, requiredBaseAsset))
    {
        return false;
    }

    if (!SaleFrame::convertToBaseAmount(quoteAsset.price, mCreateSaleCreationRequest.request.softCap, requiredBaseAsset))
    {
        return false;
    }

    return true;
}

CreateSaleCreationRequestOpFrame::CreateSaleCreationRequestOpFrame(
    Operation const& op, OperationResult& res,
    TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mCreateSaleCreationRequest(mOperation.body.createSaleCreationRequestOp())
{
}


bool
CreateSaleCreationRequestOpFrame::doApply(Application& app, LedgerDelta& delta,
                                        LedgerManager& ledgerManager)
{
    auto const& sale = mCreateSaleCreationRequest.request;
    if (sale.endTime <= ledgerManager.getCloseTime())
    {
        innerResult().code(CreateSaleCreationRequestResultCode::INVALID_END);
        return false;
    }

    auto& db = ledgerManager.getDatabase();
    auto request = createNewUpdateRequest(app, db, delta, ledgerManager.getCloseTime());
    if (!request)
    {
        innerResult().code(CreateSaleCreationRequestResultCode::REQUEST_NOT_FOUND);
        return false;
    }

    if (ReviewableRequestHelper::Instance()->isReferenceExist(db, getSourceID(), getReference(sale), request->getRequestID()))
    {
        innerResult().code(CreateSaleCreationRequestResultCode::REQUEST_OR_SALE_ALREADY_EXISTS);
        return false;
    }

    if (!areQuoteAssetsValid(db, mCreateSaleCreationRequest.request.quoteAssets, mCreateSaleCreationRequest.request.defaultQuoteAsset))
    {
        innerResult().code(CreateSaleCreationRequestResultCode::QUOTE_ASSET_NOT_FOUND);
        return false;
    }

    const auto baseAsset = tryLoadBaseAssetOrRequest(sale, db);
    if (!baseAsset)
    {
        innerResult().code(CreateSaleCreationRequestResultCode::BASE_ASSET_OR_ASSET_REQUEST_NOT_FOUND);
        return false;
    }

    if (!isBaseAssetHasSufficientIssuance(baseAsset))
    {
        return false;
    }

    if (request->getRequestID() == 0)
    {
        request->setRequestID(delta.getHeaderFrame().generateID(LedgerEntryType::REVIEWABLE_REQUEST));
        ReviewableRequestHelper::Instance()->storeAdd(delta, db, request->mEntry);
    } else
    {
        ReviewableRequestHelper::Instance()->storeChange(delta, db, request->mEntry);
    }

    innerResult().code(CreateSaleCreationRequestResultCode::SUCCESS);
    innerResult().success().requestID = request->getRequestID();
    return true;
}

bool CreateSaleCreationRequestOpFrame::doCheckValid(Application& app)
{
    const auto& request = mCreateSaleCreationRequest.request;
    if (!AssetFrame::isAssetCodeValid(request.baseAsset) || !AssetFrame::isAssetCodeValid(request.defaultQuoteAsset) 
        || request.defaultQuoteAsset == request.baseAsset || mCreateSaleCreationRequest.request.quoteAssets.empty())
    {
        innerResult().code(CreateSaleCreationRequestResultCode::INVALID_ASSET_PAIR);
        return false;
    }

    std::set<AssetCode> quoteAssets;
    for (auto const& quoteAsset : mCreateSaleCreationRequest.request.quoteAssets)
    {
        if (!AssetFrame::isAssetCodeValid(quoteAsset.quoteAsset) || quoteAsset.quoteAsset == request.baseAsset)
        {
            innerResult().code(CreateSaleCreationRequestResultCode::INVALID_ASSET_PAIR);
            return false;
        }

        if (quoteAssets.find(quoteAsset.quoteAsset) != quoteAssets.end())
        {
            innerResult().code(CreateSaleCreationRequestResultCode::INVALID_ASSET_PAIR);
            return false;
        }
        quoteAssets.insert(quoteAsset.quoteAsset);

        if (!isPriceValid(quoteAsset))
        {
            innerResult().code(CreateSaleCreationRequestResultCode::INVALID_PRICE);
            return false;
        }
    }

    if (request.endTime <= request.startTime)
    {
        innerResult().code(CreateSaleCreationRequestResultCode::START_END_INVALID);
        return false;
    }

    if (request.hardCap < request.softCap)
    {
        innerResult().code(CreateSaleCreationRequestResultCode::INVALID_CAP);
        return false;
    }

    if (!isValidJson(request.details))
    {
        innerResult().code(CreateSaleCreationRequestResultCode::INVALID_DETAILS);
        return false;
    }

    return true;
}
}
