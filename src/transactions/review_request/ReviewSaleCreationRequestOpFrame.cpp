// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <transactions/ManageAssetPairOpFrame.h>
#include "util/asio.h"
#include "ReviewSaleCreationRequestOpFrame.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "ledger/AssetHelper.h"
#include "main/Application.h"
#include "xdrpp/printer.h"
#include "ledger/SaleHelper.h"
#include "ledger/AssetPairHelper.h"

namespace stellar
{
using namespace std;
using xdr::operator==;

bool ReviewSaleCreationRequestOpFrame::handleApprove(
    Application& app, LedgerDelta& delta, LedgerManager& ledgerManager,
    ReviewableRequestFrame::pointer request)
{
    if (request->getRequestType() != ReviewableRequestType::SALE)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) <<
            "Unexpected request type. Expected SALE, but got " << xdr::
            xdr_traits<ReviewableRequestType>::
            enum_name(request->getRequestType());
        throw
            invalid_argument("Unexpected request type for review sale creation request");
    }

    auto& db = app.getDatabase();
    EntryHelperProvider::storeDeleteEntry(delta, db, request->getKey());

    auto& saleCreationRequest = request->getRequestEntry().body.saleCreationRequest();
    if (!AssetHelper::Instance()->exists(db, saleCreationRequest.quoteAsset))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state, quote asset does not exist: " << request->getRequestID();
        throw runtime_error("Quote asset does not exist");
    }

    auto baseAsset = AssetHelper::Instance()->loadAsset(saleCreationRequest.baseAsset, request->getRequestor(), db, &delta);
    if (!baseAsset)
    {
        innerResult().code(ReviewRequestResultCode::BASE_ASSET_DOES_NOT_EXISTS);
        return false;
    }

    uint64_t requiredBaseAssetForHardCap;
    if (!SaleFrame::convertToBaseAmount(saleCreationRequest.price, saleCreationRequest.hardCap, requiredBaseAssetForHardCap))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to calculate required base asset for soft cap: " << request->getRequestID();
        throw runtime_error("Failed to calculate required base asset for soft cap");
    }

    if (baseAsset->willExceedMaxIssuanceAmount(requiredBaseAssetForHardCap))
    {
        innerResult().code(ReviewRequestResultCode::HARD_CAP_WILL_EXCEED_MAX_ISSUANCE);
        return false;
    }

    if (!baseAsset->isAvailableForIssuanceAmountSufficient(requiredBaseAssetForHardCap))
    {
        innerResult().code(ReviewRequestResultCode::INSUFFICIENT_PREISSUED_FOR_HARD_CAP);
        return false;
    }

    if (!baseAsset->lockIssuedAmount(requiredBaseAssetForHardCap))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state, failed to lock issuance amount: " << request->getRequestID();
        throw runtime_error("Failed to lock issuance amount");
    }

    AssetHelper::Instance()->storeChange(delta, db, baseAsset->mEntry);

    AccountManager accountManager(app, db, delta, ledgerManager);
    const auto baseBalanceID = accountManager.loadOrCreateBalanceForAsset(request->getRequestor(), saleCreationRequest.baseAsset);
    const auto quoteBalanceID = accountManager.loadOrCreateBalanceForAsset(request->getRequestor(), saleCreationRequest.quoteAsset);

    const auto saleFrame = SaleFrame::createNew(delta.getHeaderFrame().generateID(LedgerEntryType::SALE), baseAsset->getOwner(), saleCreationRequest,
        baseBalanceID, quoteBalanceID);
    SaleHelper::Instance()->storeAdd(delta, db, saleFrame->mEntry);
    createAssetPair(saleFrame, app, ledgerManager, delta);
    innerResult().code(ReviewRequestResultCode::SUCCESS);
    return true;
}

SourceDetails ReviewSaleCreationRequestOpFrame::getSourceAccountDetails(
    std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails)
const
{
    return SourceDetails({AccountType::MASTER},
                         mSourceAccount->getHighThreshold(),
                         static_cast<int32_t>(SignerType::ASSET_MANAGER));
}

void ReviewSaleCreationRequestOpFrame::createAssetPair(SaleFrame::pointer sale, Application &app,
                                                       LedgerManager &ledgerManager, LedgerDelta &delta) const
{
    // no need to create new asset pair
    auto assetPair = AssetPairHelper::Instance()->tryLoadAssetPairForAssets(sale->getBaseAsset(), sale->getQuoteAsset(),
                                                                            ledgerManager.getDatabase());
    if (!!assetPair)
    {
        return;
    }

    //create new asset pair
    Operation op;
    op.body.type(OperationType::MANAGE_ASSET_PAIR);
    auto& manageAssetPair = op.body.manageAssetPairOp();
    manageAssetPair.action = ManageAssetPairAction::CREATE;
    manageAssetPair.base = sale->getBaseAsset();
    manageAssetPair.quote = sale->getQuoteAsset();
    manageAssetPair.physicalPrice = sale->getPrice();

    OperationResult opRes;
    opRes.code(OperationResultCode::opINNER);
    opRes.tr().type(OperationType::MANAGE_ASSET_PAIR);
    ManageAssetPairOpFrame assetPairOpFrame(op, opRes, mParentTx);
    assetPairOpFrame.setSourceAccountPtr(mSourceAccount);
    bool applied = assetPairOpFrame.doCheckValid(app) && assetPairOpFrame.doApply(app, delta, ledgerManager);
    if (!applied)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unable to create asset pair for sale creation request: " << sale->getID();
        throw std::runtime_error("Unexpected state. Unable to create asset pair");
    }

}

ReviewSaleCreationRequestOpFrame::ReviewSaleCreationRequestOpFrame(
    Operation const& op, OperationResult& res, TransactionFrame& parentTx) :
                                                                           ReviewRequestOpFrame(op,
                                                                                                res,
                                                                                                parentTx)
{
}
}
