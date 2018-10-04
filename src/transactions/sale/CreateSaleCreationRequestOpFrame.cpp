// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "CreateSaleCreationRequestOpFrame.h"
#include "database/Database.h"
#include "main/Application.h"
#include "medida/metrics_registry.h"
#include "ledger/LedgerDelta.h"
#include "ledger/LedgerHeaderFrame.h"
#include "ledger/AccountHelper.h"
#include "ledger/BalanceHelperLegacy.h"
#include "ledger/AssetHelperLegacy.h"
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

AssetFrame::pointer
CreateSaleCreationRequestOpFrame::tryLoadBaseAssetOrRequest(SaleCreationRequest const& request,
                                                            Database& db, AccountID const& source)
{
    const auto assetFrame = AssetHelperLegacy::Instance()->loadAsset(request.baseAsset, source, db);
    if (!!assetFrame)
    {
        return assetFrame;
    }

    auto assetCreationRequests = ReviewableRequestHelper::Instance()->loadRequests(source, ReviewableRequestType::ASSET_CREATE, db);
    for (auto assetCreationRequestFrame : assetCreationRequests)
    {
        auto& assetCreationRequest = assetCreationRequestFrame->getRequestEntry().body.assetCreationRequest();
        if (assetCreationRequest.code == request.baseAsset)
        {
            return AssetFrame::create(assetCreationRequest, source);
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
createNewUpdateRequest(Application& app, LedgerManager& lm, Database& db, LedgerDelta& delta, const time_t closedAt) const
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
    xdr::pointer<string64> referencePtr = nullptr;
    if (!lm.shouldUse(LedgerVersion::ALLOW_TO_CREATE_SEVERAL_SALES)) {
        auto reference = getReference(sale);
        referencePtr = xdr::pointer<string64>(new string64(reference));
    }
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
    if (!AssetHelperLegacy::Instance()->exists(db, defaultQuoteAsset))
    {
        return false;
    }

    for (auto const& quoteAsset : quoteAssets)
    {
        if (!AssetHelperLegacy::Instance()->exists(db, quoteAsset.quoteAsset))
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

bool CreateSaleCreationRequestOpFrame::isPriceValid(SaleCreationRequestQuoteAsset const& quoteAsset,
                                                    SaleCreationRequest const& saleCreationRequest)
{
    if (quoteAsset.price == 0)
    {
        return false;
    }

    bool isCrowdfunding = saleCreationRequest.ext.v() == LedgerVersion::TYPED_SALE &&
            saleCreationRequest.ext.saleTypeExt().typedSale.saleType() == SaleType::CROWD_FUNDING;
    isCrowdfunding = isCrowdfunding || (saleCreationRequest.ext.v() == LedgerVersion::STATABLE_SALES &&
            saleCreationRequest.ext.extV3().saleTypeExt.typedSale.saleType() == SaleType::CROWD_FUNDING);
    if (isCrowdfunding)
    {
        return quoteAsset.price == ONE;
    }

    uint64_t requiredBaseAsset;
    if (!SaleFrame::convertToBaseAmount(quoteAsset.price, saleCreationRequest.hardCap, requiredBaseAsset))
    {
        return false;
    }

    if (!SaleFrame::convertToBaseAmount(quoteAsset.price, saleCreationRequest.softCap, requiredBaseAsset))
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

    auto saleVersion = static_cast<int32>(sale.ext.v());
    if (saleVersion > app.getLedgerManager().getCurrentLedgerHeader().ledgerVersion)
    {
        innerResult().code(CreateSaleCreationRequestResultCode::VERSION_IS_NOT_SUPPORTED_YET);
        return false;
    }

    if (sale.endTime <= ledgerManager.getCloseTime())
    {
        innerResult().code(CreateSaleCreationRequestResultCode::INVALID_END);
        return false;
    }

    auto& db = ledgerManager.getDatabase();
    auto request = createNewUpdateRequest(app, ledgerManager, db, delta, ledgerManager.getCloseTime());
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

    const auto baseAsset = tryLoadBaseAssetOrRequest(sale, db, getSourceID());
    if (!baseAsset)
    {
        innerResult().code(CreateSaleCreationRequestResultCode::BASE_ASSET_OR_ASSET_REQUEST_NOT_FOUND);
        return false;
    }
    if (!ensureEnoughAvailable(app, sale, baseAsset))
    {
        innerResult().code(CreateSaleCreationRequestResultCode::INSUFFICIENT_PREISSUED);
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

bool CreateSaleCreationRequestOpFrame::ensureEnoughAvailable(Application& app,
                                                             const SaleCreationRequest& saleCreationRequest,
                                                             AssetFrame::pointer baseAsset)
{
    if(!app.getLedgerManager().shouldUse(LedgerVersion::STATABLE_SALES))
        return true;
    SaleType saleType;
    switch (saleCreationRequest.ext.v()) {
        case LedgerVersion::ALLOW_TO_SPECIFY_REQUIRED_BASE_ASSET_AMOUNT_FOR_HARD_CAP: {
            saleType = saleCreationRequest.ext.extV2().saleTypeExt.typedSale.saleType();
            break;
        }
        case LedgerVersion::STATABLE_SALES: {
            saleType = saleCreationRequest.ext.extV3().saleTypeExt.typedSale.saleType();
            break;
        }
        default: {
            return true;
        }
    }
    if (saleType != SaleType::FIXED_PRICE)
        return true;

    return baseAsset->getAvailableForIssuance() >= saleCreationRequest.ext.extV3().requiredBaseAssetForHardCap;
}

bool CreateSaleCreationRequestOpFrame::doCheckValid(Application& app)
{
    auto checkValidResult = doCheckValid(app, mCreateSaleCreationRequest.request, getSourceID());
    if (checkValidResult == CreateSaleCreationRequestResultCode::SUCCESS) {
        return true;
    }

    innerResult().code(checkValidResult);
    return false;
}

CreateSaleCreationRequestResultCode
CreateSaleCreationRequestOpFrame::doCheckValid(Application &app, const SaleCreationRequest &saleCreationRequest,
                                               AccountID const& source)
{
    if (!AssetFrame::isAssetCodeValid(saleCreationRequest.baseAsset) ||
        !AssetFrame::isAssetCodeValid(saleCreationRequest.defaultQuoteAsset)
        || saleCreationRequest.defaultQuoteAsset == saleCreationRequest.baseAsset
        || saleCreationRequest.quoteAssets.empty())
    {
        return CreateSaleCreationRequestResultCode::INVALID_ASSET_PAIR;
    }

    std::set<AssetCode> quoteAssets;
    for (auto const& quoteAsset : saleCreationRequest.quoteAssets)
    {
        if (!AssetFrame::isAssetCodeValid(quoteAsset.quoteAsset) ||
            quoteAsset.quoteAsset == saleCreationRequest.baseAsset)
        {
            return CreateSaleCreationRequestResultCode::INVALID_ASSET_PAIR;
        }

        if (quoteAssets.find(quoteAsset.quoteAsset) != quoteAssets.end())
        {
            return CreateSaleCreationRequestResultCode::INVALID_ASSET_PAIR;
        }
        quoteAssets.insert(quoteAsset.quoteAsset);

        if (!isPriceValid(quoteAsset, saleCreationRequest))
        {
            return CreateSaleCreationRequestResultCode::INVALID_PRICE;
        }
    }

    if (saleCreationRequest.endTime <= saleCreationRequest.startTime)
    {
        return CreateSaleCreationRequestResultCode::START_END_INVALID;
    }

    if (saleCreationRequest.hardCap < saleCreationRequest.softCap)
    {
        return CreateSaleCreationRequestResultCode::INVALID_CAP;
    }

    if (!isValidJson(saleCreationRequest.details))
    {
        return CreateSaleCreationRequestResultCode::INVALID_DETAILS;
    }

    return CreateSaleCreationRequestResultCode::SUCCESS;
}

}
