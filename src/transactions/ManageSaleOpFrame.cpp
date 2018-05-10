#include "ManageSaleOpFrame.h"
#include <ledger/SaleHelper.h>
#include <ledger/ReviewableRequestFrame.h>
#include <ledger/ReviewableRequestHelper.h>

namespace stellar {
    ManageSaleOpFrame::ManageSaleOpFrame(Operation const &op, OperationResult &opRes, TransactionFrame &parentTx)
            : OperationFrame(op, opRes, parentTx), mManageSaleOp(mOperation.body.manageSaleOp()) {
    }

    std::unordered_map<AccountID, CounterpartyDetails>
    ManageSaleOpFrame::getCounterpartyDetails(Database &db, LedgerDelta *delta) const {
        // no counterparties
        return {};
    }

    SourceDetails
    ManageSaleOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                               int32_t ledgerVersion) const {
        return SourceDetails({AccountType::SYNDICATE}, mSourceAccount->getHighThreshold(),
                             static_cast<int32_t>(SignerType::ASSET_MANAGER));
    }

    bool
    ManageSaleOpFrame::createUpdateSaleDetailsRequest(Application &app, LedgerDelta &delta,
                                                      LedgerManager &ledgerManager, Database &db) {
        auto reference = getUpdateSaleDetailsRequestReference();
        auto const referencePtr = xdr::pointer<string64>(new string64(reference));
        auto requestHelper = ReviewableRequestHelper::Instance();
        if (requestHelper->isReferenceExist(db, getSourceID(), reference)) {
            //TODO: amendUpdateSaleDetailsRequest
        }

        auto requestFrame = ReviewableRequestFrame::createNew(delta, getSourceID(), app.getMasterID(),
                                                              referencePtr, ledgerManager.getCloseTime());
        auto &requestEntry = requestFrame->getRequestEntry();
        requestEntry.body.type(ReviewableRequestType::UPDATE_SALE_DETAILS);
        requestEntry.body.updateSaleDetailsRequest().saleID = mManageSaleOp.saleID;
        requestEntry.body.updateSaleDetailsRequest().newDetails = mManageSaleOp.data.newDetails();

        requestFrame->recalculateHashRejectReason();

        requestHelper->storeAdd(delta, db, requestFrame->mEntry);

        innerResult().code(ManageSaleResultCode::SUCCESS);
        innerResult().success().response.action(ManageSaleAction::CREATE_UPDATE_DETAILS_REQUEST);
        innerResult().success().response.requestID() = requestFrame->getRequestID();

        return true;
    }

    bool ManageSaleOpFrame::amendUpdateSaleDetailsRequest() {
        return true;
    }

    bool ManageSaleOpFrame::doApply(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager) {
        Database &db = app.getDatabase();
        auto saleFrame = SaleHelper::Instance()->loadSale(mManageSaleOp.saleID, getSourceID(), db, &delta);
        if (!saleFrame) {
            innerResult().code(ManageSaleResultCode::NOT_FOUND);
            return false;
        }

        switch (mManageSaleOp.data.action()) {
            case ManageSaleAction::CREATE_UPDATE_DETAILS_REQUEST:

                break;
            default:
                CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected action from manage sale op: "
                                                       << xdr::xdr_to_string(mManageSaleOp.data.action());
                throw std::runtime_error("Unexpected action from manage sale op");
        }

        innerResult().code(ManageSaleResultCode::SUCCESS);
        return true;
    }

    bool ManageSaleOpFrame::doCheckValid(Application &app) {
        if (mManageSaleOp.saleID == 0) {
            innerResult().code(ManageSaleResultCode::NOT_FOUND);
            return false;
        }

        if (mManageSaleOp.data.action() != ManageSaleAction::CREATE_UPDATE_DETAILS_REQUEST) {
            return true;
        }

        if (!isValidJson(mManageSaleOp.data.newDetails())) {
            innerResult().code(ManageSaleResultCode::INVALID_NEW_DETAILS);
            return false;
        }

        return true;
    }
}
