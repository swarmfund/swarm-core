// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/asio.h"
#include "ReviewSaleCreationRequestOpFrame.h"
#include "util/Logging.h"
#include "util/types.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "ledger/ReviewableRequestFrame.h"
#include "ledger/ReferenceFrame.h"
#include "ledger/AssetHelper.h"
#include "ledger/BalanceHelper.h"
#include "main/Application.h"
#include "xdrpp/printer.h"
#include "ledger/SaleFrame.h"
#include "ledger/SaleHelper.h"

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
        innerResult().code(ReviewRequestResultCode::QUOTE_ASSET_DOES_NOT_EXISTS);
        return false;
    }

    auto baseAsset = AssetHelper::Instance()->loadAsset(saleCreationRequest.baseAsset, request->getRequestor(), db, &delta);
    if (!baseAsset)
    {
        innerResult().code(ReviewRequestResultCode::BASE_ASSET_DOES_NOT_EXISTS);
        return false;
    }

    uint64_t requiredBaseAssetForSoftCap;
    if (!SaleFrame::calculateRequiredBaseAssetForSoftCap(saleCreationRequest, requiredBaseAssetForSoftCap))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to calculate required base asset for soft cap: " << request->getRequestID();
        throw runtime_error("Failed to calculate required base asset for soft cap");
    }

    
    if (!baseAsset->lockIssuedAmount(requiredBaseAssetForSoftCap))
    {
        innerResult().code(ReviewRequestResultCode::SOFT_CAP_WILL_EXCEED_MAX_ISSUANCE);
        return false;
    }

    AssetHelper::Instance()->storeChange(delta, db, baseAsset->mEntry);

    const auto saleFrame = SaleFrame::createNew(delta.getHeaderFrame().generateID(LedgerEntryType::SALE), baseAsset->getOwner(), saleCreationRequest);
    SaleHelper::Instance()->storeAdd(delta, db, saleFrame->mEntry);

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

ReviewSaleCreationRequestOpFrame::ReviewSaleCreationRequestOpFrame(
    Operation const& op, OperationResult& res, TransactionFrame& parentTx) :
                                                                           ReviewRequestOpFrame(op,
                                                                                                res,
                                                                                                parentTx)
{
}
}
