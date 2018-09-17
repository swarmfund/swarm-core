
#include <ledger/ReviewableRequestHelper.h>
#include "CancelSaleCreationRequestOpFrame.h"

namespace stellar
{
using xdr::operator==;


std::unordered_map<AccountID, CounterpartyDetails>
CancelSaleCreationRequestOpFrame::getCounterpartyDetails(
        Database& db, LedgerDelta* delta) const
{
    // source account is only counterparty
    return {};
}

SourceDetails
CancelSaleCreationRequestOpFrame::getSourceAccountDetails(
    std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
    int32_t ledgerVersion) const
{
    return SourceDetails({AccountType::SYNDICATE},
                         mSourceAccount->getHighThreshold(),
                         static_cast<int32_t>(SignerType::ASSET_MANAGER));
}

CancelSaleCreationRequestOpFrame::CancelSaleCreationRequestOpFrame(
        Operation const& op, OperationResult& res,
        TransactionFrame& parentTx)
        : OperationFrame(op, res, parentTx),
        mCancelSaleCreationRequest(mOperation.body.cancelSaleCreationRequestOp())
{
}


bool
CancelSaleCreationRequestOpFrame::doApply(Application& app, LedgerDelta& delta,
                                          LedgerManager& ledgerManager)
{
    auto const requestID = mCancelSaleCreationRequest.requestID;
    auto& db = ledgerManager.getDatabase();
    auto requestHelper = ReviewableRequestHelper::Instance();

    auto requestFrame = requestHelper->loadRequest(requestID, getSourceID(),
            ReviewableRequestType::SALE, db, &delta);
    if (!requestFrame)
    {
        innerResult().code(CancelSaleCreationRequestResultCode::REQUEST_NOT_FOUND);
        return false;
    }

    requestHelper->storeDelete(delta, db, requestFrame->getKey());

    innerResult().code(CancelSaleCreationRequestResultCode::SUCCESS);
    return true;
}

bool
CancelSaleCreationRequestOpFrame::doCheckValid(Application& app)
{
    if (mCancelSaleCreationRequest.requestID == 0) {
        innerResult().code(CancelSaleCreationRequestResultCode::REQUEST_ID_INVALID);
        return false;
    }

    return true;
}
}

