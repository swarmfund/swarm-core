// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ReviewSaleCreationRequestOpFrame.h"
#include "database/Database.h"
#include "ledger/LedgerHeaderFrame.h"
#include "ledger/AssetHelper.h"
#include "ledger/AssetPairHelper.h"
#include "ledger/LedgerDelta.h"
#include "ledger/SaleHelper.h"
#include "main/Application.h"
#include "transactions/CreateSaleCreationRequestOpFrame.h"
#include "xdrpp/printer.h"
#include <ledger/AccountHelper.h>
#include <transactions/ManageAssetPairOpFrame.h>

namespace stellar
{
using namespace std;
using xdr::operator==;

SaleCreationRequest&
ReviewSaleCreationRequestOpFrame::getSaleCreationRequestFromBody(
    ReviewableRequestFrame::pointer request)
{
    auto requestType = request->getType();
    switch (requestType)
    {
    case ReviewableRequestType::SALE:
    {
        return request->getRequestEntry().body.saleCreationRequest();
    }
    case ReviewableRequestType::UPDATE_PROMOTION:
    {
        return request->getRequestEntry()
            .body.promotionUpdateRequest()
            .newPromotionData;
    }
    default:
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER)
            << "Unexpected reviewable request type while retrieving sale "
               "creation request from body: "
            << request->getRequestID();
        throw runtime_error("Unexpected reviewable request type while "
                            "retrieving sale creation request from body");
    }
    }
}

ReviewRequestResultCode
ReviewSaleCreationRequestOpFrame::tryCreateSale(
    Application& app, Database& db, LedgerDelta& delta,
    LedgerManager& ledgerManager, ReviewableRequestFrame::pointer request,
    uint64_t saleID)
{
    auto saleCreationRequest = getSaleCreationRequestFromBody(request);

    if (!CreateSaleCreationRequestOpFrame::areQuoteAssetsValid(
            db, saleCreationRequest.quoteAssets,
            saleCreationRequest.defaultQuoteAsset))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER)
            << "Unexpected state, quote asset does not exist: "
            << request->getRequestID();
        throw runtime_error("Quote asset does not exist");
    }

    auto baseAsset = loadAsset(ledgerManager, saleCreationRequest.baseAsset,
                               request->getRequestor(), db, &delta);
    if (!baseAsset)
    {
        return ReviewRequestResultCode::BASE_ASSET_DOES_NOT_EXISTS;
    }

    const uint64_t requiredBaseAssetForHardCap =
        saleCreationRequest.ext.v() >=
                LedgerVersion::
                    ALLOW_TO_SPECIFY_REQUIRED_BASE_ASSET_AMOUNT_FOR_HARD_CAP
            ? getRequiredBaseAssetForHardCap(saleCreationRequest)
            : baseAsset->getMaxIssuanceAmount();

    if (!baseAsset->lockIssuedAmount(requiredBaseAssetForHardCap))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER)
            << "Unexpected state, failed to lock issuance amount: "
            << request->getRequestID();
        return ReviewRequestResultCode::INSUFFICIENT_PREISSUED_FOR_HARD_CAP;
    }

    AssetHelper::Instance()->storeChange(delta, db, baseAsset->mEntry);

    AccountManager accountManager(app, db, delta, ledgerManager);
    const auto balances =
        loadBalances(accountManager, request, saleCreationRequest);
    const auto saleFrame =
        SaleFrame::createNew(saleID, baseAsset->getOwner(), saleCreationRequest,
                             balances, requiredBaseAssetForHardCap);
    SaleHelper::Instance()->storeAdd(delta, db, saleFrame->mEntry);
    createAssetPair(saleFrame, app, ledgerManager, delta);
    return ReviewRequestResultCode::SUCCESS;
}

AssetFrame::pointer
ReviewSaleCreationRequestOpFrame::loadAsset(LedgerManager& ledgerManager,
                                            AssetCode code,
                                            AccountID const& requestor,
                                            Database& db, LedgerDelta* delta)
{
    AssetFrame::pointer retAsset;

    if (!ledgerManager.shouldUse(
            LedgerVersion::ALLOW_TO_UPDATE_VOTING_SALES_AS_PROMOTION))
    {
        retAsset =
            AssetHelper::Instance()->loadAsset(code, requestor, db, delta);
        return retAsset;
    }

    auto requestorFrame =
        AccountHelper::Instance()->mustLoadAccount(requestor, db);

    if (requestorFrame->getAccountType() == AccountType::MASTER)
    {
        retAsset = AssetHelper::Instance()->loadAsset(code, db, delta);
        return retAsset;
    }

    retAsset = AssetHelper::Instance()->loadAsset(code, requestor, db, delta);
    return retAsset;
}

bool
ReviewSaleCreationRequestOpFrame::handleApprove(
    Application& app, LedgerDelta& delta, LedgerManager& ledgerManager,
    ReviewableRequestFrame::pointer request)
{
    if (request->getRequestType() != ReviewableRequestType::SALE)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER)
            << "Unexpected request type. Expected SALE, but got "
            << xdr::xdr_traits<ReviewableRequestType>::enum_name(
                   request->getRequestType());
        throw invalid_argument(
            "Unexpected request type for review sale creation request");
    }

    auto& db = app.getDatabase();
    EntryHelperProvider::storeDeleteEntry(delta, db, request->getKey());

    auto newSaleID = delta.getHeaderFrame().generateID(LedgerEntryType::SALE);

    ReviewRequestResultCode saleCreationResult =
        tryCreateSale(app, db, delta, ledgerManager, request, newSaleID);

    innerResult().code(saleCreationResult);

    if (saleCreationResult != ReviewRequestResultCode::SUCCESS)
    {
        return false;
    }

    if (ledgerManager.shouldUse(LedgerVersion::ADD_TASKS_TO_REVIEWABLE_REQUEST))
    {
        innerResult().success().ext.v(LedgerVersion::ADD_TASKS_TO_REVIEWABLE_REQUEST);
        innerResult().success().ext.extendedResult().fulfilled = true;
        innerResult().success().ext.extendedResult().typeExt.requestType(ReviewableRequestType::SALE);
        innerResult().success().ext.extendedResult().typeExt.saleExtended().saleID = newSaleID;
        return true;
    }

    if (ledgerManager.shouldUse(LedgerVersion::ADD_SALE_ID_REVIEW_REQUEST_RESULT))
    {
        innerResult().success().ext.v(
            LedgerVersion::ADD_SALE_ID_REVIEW_REQUEST_RESULT);
        innerResult().success().ext.saleID() = newSaleID;
    }

    return true;
}

SourceDetails
ReviewSaleCreationRequestOpFrame::getSourceAccountDetails(
    std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
    int32_t ledgerVersion) const
{
    auto allowedSigners = static_cast<int32_t>(SignerType::ASSET_MANAGER);

    auto newSignersVersion =
        static_cast<int32_t>(LedgerVersion::NEW_SIGNER_TYPES);
    if (ledgerVersion >= newSignersVersion)
    {
        allowedSigners = static_cast<int32_t>(SignerType::USER_ASSET_MANAGER);
    }

    return SourceDetails({AccountType::MASTER},
                         mSourceAccount->getHighThreshold(), allowedSigners);
}

uint64
ReviewSaleCreationRequestOpFrame::getRequiredBaseAssetForHardCap(
    SaleCreationRequest const& saleCreationRequest)
{
    switch (saleCreationRequest.ext.v())
    {
    case LedgerVersion::
        ALLOW_TO_SPECIFY_REQUIRED_BASE_ASSET_AMOUNT_FOR_HARD_CAP:
        return saleCreationRequest.ext.extV2().requiredBaseAssetForHardCap;
    case LedgerVersion::STATABLE_SALES:
        return saleCreationRequest.ext.extV3().requiredBaseAssetForHardCap;
    default:
        throw std::runtime_error("Unexpected operation: trying to get "
                                 "requiredBaseAssetForHardCap from unknown "
                                 "version of the request");
    }
}

void
ReviewSaleCreationRequestOpFrame::createAssetPair(SaleFrame::pointer sale,
                                                  Application& app,
                                                  LedgerManager& ledgerManager,
                                                  LedgerDelta& delta) const
{
    for (const auto quoteAsset : sale->getSaleEntry().quoteAssets)
    {
        const auto assetPair =
            AssetPairHelper::Instance()->tryLoadAssetPairForAssets(
                sale->getBaseAsset(), quoteAsset.quoteAsset,
                ledgerManager.getDatabase());
        if (!!assetPair)
        {
            if (ledgerManager.shouldUse(
                    LedgerVersion::FIX_ASSET_PAIRS_CREATION_IN_SALE_CREATION))
            {
                continue;
            }
            return;
        }

        // create new asset pair
        Operation op;
        op.body.type(OperationType::MANAGE_ASSET_PAIR);
        auto& manageAssetPair = op.body.manageAssetPairOp();
        manageAssetPair.action = ManageAssetPairAction::CREATE;
        manageAssetPair.base = sale->getBaseAsset();
        manageAssetPair.quote = quoteAsset.quoteAsset;
        manageAssetPair.physicalPrice = quoteAsset.price;

        OperationResult opRes;
        opRes.code(OperationResultCode::opINNER);
        opRes.tr().type(OperationType::MANAGE_ASSET_PAIR);
        ManageAssetPairOpFrame assetPairOpFrame(op, opRes, mParentTx);
        assetPairOpFrame.setSourceAccountPtr(mSourceAccount);
        const auto applied =
            assetPairOpFrame.doCheckValid(app) &&
            assetPairOpFrame.doApply(app, delta, ledgerManager);
        if (!applied)
        {
            CLOG(ERROR, Logging::OPERATION_LOGGER)
                << "Unable to create asset pair for sale creation request: "
                << sale->getID();
            throw runtime_error(
                "Unexpected state. Unable to create asset pair");
        }
    }
}

std::map<AssetCode, BalanceID>
ReviewSaleCreationRequestOpFrame::loadBalances(
    AccountManager& accountManager,
    const ReviewableRequestFrame::pointer request,
    SaleCreationRequest const& saleCreationRequest)
{
    map<AssetCode, BalanceID> result;
    const auto baseBalanceID = accountManager.loadOrCreateBalanceForAsset(
        request->getRequestor(), saleCreationRequest.baseAsset);
    result.insert(make_pair(saleCreationRequest.baseAsset, baseBalanceID));
    for (auto quoteAsset : saleCreationRequest.quoteAssets)
    {
        const auto quoteBalanceID = accountManager.loadOrCreateBalanceForAsset(
            request->getRequestor(), quoteAsset.quoteAsset);
        result.insert(make_pair(quoteAsset.quoteAsset, quoteBalanceID));
    }
    return result;
}

ReviewSaleCreationRequestOpFrame::ReviewSaleCreationRequestOpFrame(
    Operation const& op, OperationResult& res, TransactionFrame& parentTx)
    : ReviewRequestOpFrame(op, res, parentTx)
{
}
}
