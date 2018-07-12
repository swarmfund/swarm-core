#include <transactions/dex/ManageSaleOpFrame.h>
#include <ledger/SaleHelper.h>
#include "ReviewUpdateSaleEndTimeRequestOpFrame.h"

namespace stellar {

    ReviewUpdateSaleEndTimeRequestOpFrame::ReviewUpdateSaleEndTimeRequestOpFrame(Operation const &op,
                                                                                 OperationResult &res,
                                                                                 TransactionFrame &parentTx)
            : ReviewRequestOpFrame(op, res, parentTx) {
    }

    SourceDetails ReviewUpdateSaleEndTimeRequestOpFrame::getSourceAccountDetails(
            std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails, int32_t ledgerVersion) const {
        return SourceDetails({AccountType::MASTER}, mSourceAccount->getHighThreshold(),
                             static_cast<int32_t>(SignerType::ASSET_MANAGER));
    }

    bool ReviewUpdateSaleEndTimeRequestOpFrame::handleApprove(Application &app, LedgerDelta &delta,
                                                              LedgerManager &ledgerManager,
                                                              ReviewableRequestFrame::pointer request) {
        ManageSaleOpFrame::checkRequestType(request, ReviewableRequestType::UPDATE_SALE_END_TIME);

        Database &db = ledgerManager.getDatabase();

        auto &updateSaleEndTimeRequest = request->getRequestEntry().body.updateSaleEndTimeRequest();

        auto saleFrame = SaleHelper::Instance()->loadSale(updateSaleEndTimeRequest.saleID, db, &delta);

        if (!saleFrame) {
            innerResult().code(ReviewRequestResultCode::SALE_NOT_FOUND);
            return false;
        }

        if (!saleFrame->isEndTimeValid(updateSaleEndTimeRequest.newEndTime, ledgerManager.getCloseTime())) {
            innerResult().code(ReviewRequestResultCode::INVALID_SALE_NEW_END_TIME);
            return false;
        }

        saleFrame->getSaleEntry().endTime = updateSaleEndTimeRequest.newEndTime;

        EntryHelperProvider::storeChangeEntry(delta, db, saleFrame->mEntry);
        EntryHelperProvider::storeDeleteEntry(delta, db, request->getKey());

        innerResult().code(ReviewRequestResultCode::SUCCESS);
        return true;
    }
}