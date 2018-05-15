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

    bool ManageSaleOpFrame::amendUpdateSaleDetailsRequest(Database &db, LedgerDelta &delta) {
        auto requestFrame = ReviewableRequestHelper::Instance()->loadRequest(
                mManageSaleOp.data.updateSaleDetailsData().requestID, db, &delta);

        if (!requestFrame) {
            innerResult().code(ManageSaleResultCode::UPDATE_DETAILS_REQUEST_NOT_FOUND);
            return false;
        }

        checkRequestType(requestFrame);

        auto &requestEntry = requestFrame->getRequestEntry();
        requestEntry.body.updateSaleDetailsRequest().newDetails = mManageSaleOp.data.updateSaleDetailsData().newDetails;

        requestFrame->recalculateHashRejectReason();
        ReviewableRequestHelper::Instance()->storeChange(delta, db, requestFrame->mEntry);

        innerResult().code(ManageSaleResultCode::SUCCESS);
        innerResult().success().response.action(ManageSaleAction::CREATE_UPDATE_DETAILS_REQUEST);
        innerResult().success().response.requestID() = requestFrame->getRequestID();

        return true;
    }

    bool
    ManageSaleOpFrame::createUpdateSaleDetailsRequest(Application &app, LedgerDelta &delta,
                                                      LedgerManager &ledgerManager, Database &db) {
        auto reference = getUpdateSaleDetailsRequestReference();
        auto const referencePtr = xdr::pointer<string64>(new string64(reference));
        auto requestHelper = ReviewableRequestHelper::Instance();
        if (requestHelper->isReferenceExist(db, getSourceID(), reference)) {
            innerResult().code(ManageSaleResultCode::UPDATE_DETAILS_REQUEST_ALREADY_EXISTS);
            return false;
        }

        auto requestFrame = ReviewableRequestFrame::createNew(delta, getSourceID(), app.getMasterID(),
                                                              referencePtr, ledgerManager.getCloseTime());
        auto &requestEntry = requestFrame->getRequestEntry();
        requestEntry.body.type(ReviewableRequestType::UPDATE_SALE_DETAILS);
        requestEntry.body.updateSaleDetailsRequest().saleID = mManageSaleOp.saleID;
        requestEntry.body.updateSaleDetailsRequest().newDetails = mManageSaleOp.data.updateSaleDetailsData().newDetails;

        requestFrame->recalculateHashRejectReason();

        requestHelper->storeAdd(delta, db, requestFrame->mEntry);

        innerResult().code(ManageSaleResultCode::SUCCESS);
        innerResult().success().response.action(ManageSaleAction::CREATE_UPDATE_DETAILS_REQUEST);
        innerResult().success().response.requestID() = requestFrame->getRequestID();

        return true;
    }

    bool ManageSaleOpFrame::doApply(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager) {
        Database &db = app.getDatabase();
        if (!SaleHelper::Instance()->exists(db, mManageSaleOp.saleID)) {
            innerResult().code(ManageSaleResultCode::SALE_NOT_FOUND);
            return false;
        }

        switch (mManageSaleOp.data.action()) {
            case ManageSaleAction::CREATE_UPDATE_DETAILS_REQUEST: {
                if (mManageSaleOp.data.updateSaleDetailsData().requestID != 0) {
                    return amendUpdateSaleDetailsRequest(db, delta);
                }
                return createUpdateSaleDetailsRequest(app, delta, ledgerManager, db);
            }
            default:
                CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected action from manage sale op: "
                                                       << xdr::xdr_to_string(mManageSaleOp.data.action());
                throw std::runtime_error("Unexpected action from manage sale op");
        }
    }

    bool ManageSaleOpFrame::doCheckValid(Application &app) {
        if (mManageSaleOp.saleID == 0) {
            innerResult().code(ManageSaleResultCode::SALE_NOT_FOUND);
            return false;
        }

        if (mManageSaleOp.data.action() != ManageSaleAction::CREATE_UPDATE_DETAILS_REQUEST) {
            return true;
        }

        if (!isValidJson(mManageSaleOp.data.updateSaleDetailsData().newDetails)) {
            innerResult().code(ManageSaleResultCode::INVALID_NEW_DETAILS);
            return false;
        }

        return true;
    }

    void ManageSaleOpFrame::checkRequestType(ReviewableRequestFrame::pointer request) {
        if (request->getRequestType() != ReviewableRequestType::UPDATE_SALE_DETAILS) {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected request type. Expected UPDATE_SALE_DETAILS, but got "
                                                   << xdr::xdr_traits<ReviewableRequestType>::enum_name(
                                                           request->getRequestType());
            throw std::invalid_argument("Unexpected request type");
        }
    }
}
