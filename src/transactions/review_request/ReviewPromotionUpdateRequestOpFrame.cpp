#include "ledger/AccountHelper.h"
#include "ledger/SaleHelper.h"
#include "ReviewPromotionUpdateRequestOpFrame.h"
#include "transactions/dex/ManageSaleOpFrame.h"

namespace stellar {

    ReviewPromotionUpdateRequestOpFrame::ReviewPromotionUpdateRequestOpFrame(Operation const &op, OperationResult &res,
                                                                             TransactionFrame &parentTx)
            : ReviewSaleCreationRequestOpFrame(op, res, parentTx) {
    }

    SourceDetails ReviewPromotionUpdateRequestOpFrame::getSourceAccountDetails(
            std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails, int32_t ledgerVersion) const {
        return SourceDetails({AccountType::MASTER}, mSourceAccount->getHighThreshold(),
                             static_cast<int32_t>(SignerType::ASSET_MANAGER));
    }

    bool ReviewPromotionUpdateRequestOpFrame::handleApprove(Application &app, LedgerDelta &delta,
                                                            LedgerManager &ledgerManager,
                                                            ReviewableRequestFrame::pointer request) {
        ManageSaleOpFrame::checkRequestType(request, ReviewableRequestType::UPDATE_PROMOTION);

        Database &db = ledgerManager.getDatabase();

        auto &promotionUpdateRequest = request->getRequestEntry().body.promotionUpdateRequest();

        auto saleFrame = SaleHelper::Instance()->loadSale(promotionUpdateRequest.promotionID, db, &delta);

        if (!saleFrame) {
            innerResult().code(ReviewRequestResultCode::SALE_NOT_FOUND);
            return false;
        }

        if (saleFrame->getState() != SaleState::PROMOTION) {
            innerResult().code(ReviewRequestResultCode::INVALID_SALE_STATE);
            return false;
        }

        Operation op;
        op.sourceAccount.activate() = getSourceID();
        op.body.type(OperationType::MANAGE_SALE);
        ManageSaleOp &manageSaleOp = op.body.manageSaleOp();
        manageSaleOp.saleID = promotionUpdateRequest.promotionID;
        manageSaleOp.data.action(ManageSaleAction::CANCEL);

        OperationResult opRes;
        opRes.code(OperationResultCode::opINNER);
        opRes.tr().type(OperationType::MANAGE_SALE);

        ManageSaleOpFrame manageSaleOpFrame(op, opRes, mParentTx);

        auto sourceAccountFrame = AccountHelper::Instance()->mustLoadAccount(getSourceID(), db);
        manageSaleOpFrame.setSourceAccountPtr(sourceAccountFrame);

        if (!manageSaleOpFrame.doCheckValid(app) || !manageSaleOpFrame.doApply(app, delta, ledgerManager)) {
            CLOG(ERROR, Logging::OPERATION_LOGGER)
                    << "Failed to apply manage sale on review promotion update request: "
                    << promotionUpdateRequest.promotionID;
            throw std::runtime_error("Failed to apply manage sale on review promotion update request");
        }

        EntryHelperProvider::storeDeleteEntry(delta, db, request->getKey());

        ReviewRequestResultCode saleCreationResult = tryCreateSale(app, db, delta, ledgerManager, request,
                                                                   promotionUpdateRequest.promotionID);

        innerResult().code(saleCreationResult);

        return saleCreationResult == ReviewRequestResultCode::SUCCESS;
    }
}