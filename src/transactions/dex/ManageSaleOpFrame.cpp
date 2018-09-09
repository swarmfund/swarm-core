#include "ManageSaleOpFrame.h"
#include "OfferManager.h"
#include "transactions/sale/CreateSaleCreationRequestOpFrame.h"
#include <ledger/BalanceHelper.h>
#include <ledger/OfferHelper.h>
#include <ledger/ReviewableRequestHelper.h>
#include <ledger/SaleAnteHelper.h>
#include <ledger/SaleHelper.h>
#include <transactions/review_request/ReviewRequestHelper.h>

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
        std::vector<AccountType> allowedSourceAccountTypes = {AccountType::SYNDICATE};

        if (ledgerVersion >= static_cast<int32_t>(LedgerVersion::ALLOW_MASTER_TO_MANAGE_SALE)) {
            allowedSourceAccountTypes.push_back(AccountType::MASTER);
        }

        return SourceDetails(allowedSourceAccountTypes, mSourceAccount->getHighThreshold(),
                             static_cast<int32_t>(SignerType::ASSET_MANAGER));
    }

    bool ManageSaleOpFrame::amendUpdateSaleDetailsRequest(LedgerManager &lm, Database &db, LedgerDelta &delta) {
        auto requestFrame = ReviewableRequestHelper::Instance()->loadRequest(
                mManageSaleOp.data.updateSaleDetailsData().requestID, getSourceID(),
                ReviewableRequestType::UPDATE_SALE_DETAILS, db, &delta);

        if (!requestFrame) {
            innerResult().code(ManageSaleResultCode::UPDATE_DETAILS_REQUEST_NOT_FOUND);
            return false;
        }

        auto &requestEntry = requestFrame->getRequestEntry();
        requestEntry.body.updateSaleDetailsRequest().newDetails = mManageSaleOp.data.updateSaleDetailsData().newDetails;

        requestFrame->recalculateHashRejectReason();
        ReviewableRequestHelper::Instance()->storeChange(delta, db, requestFrame->mEntry);

        innerResult().code(ManageSaleResultCode::SUCCESS);
        innerResult().success().response.action(ManageSaleAction::CREATE_UPDATE_DETAILS_REQUEST);
        innerResult().success().response.requestID() = requestFrame->getRequestID();

        trySetFulfilled(lm, false);

        return true;
    }

    bool ManageSaleOpFrame::setSaleState(SaleFrame::pointer sale, Application &app, LedgerDelta &delta,
                                         LedgerManager &ledgerManager, Database &db) {
        if (mSourceAccount->getAccountType() != AccountType::MASTER) {
            innerResult().code(ManageSaleResultCode::NOT_ALLOWED);
            if (ledgerManager.shouldUse(LedgerVersion::FIX_SET_SALE_STATE_AND_CHECK_SALE_STATE_OPS)) {
                return false;
            }
        }

        sale->migrateToVersion(LedgerVersion::STATABLE_SALES);
        auto stateToSet = mManageSaleOp.data.saleState();
        switch (sale->getState()) {
            case SaleState::NONE:
                innerResult().code(ManageSaleResultCode::NOT_ALLOWED);
                return false;
            case SaleState::PROMOTION:
                break;
            case SaleState::VOTING:
                if (stateToSet == SaleState::PROMOTION) {
                    innerResult().code(ManageSaleResultCode::NOT_ALLOWED);
                    return false;
                }
                break;
            default:
                throw std::runtime_error("Unexpected sale state on manage sale set sale state");
        }

        sale->setSaleState(stateToSet);
        SaleHelper::Instance()->storeChange(delta, db, sale->mEntry);
        innerResult().code(ManageSaleResultCode::SUCCESS);
        innerResult().success().response.action(ManageSaleAction::SET_STATE);

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

        trySetFulfilled(ledgerManager, false);

        return true;
    }

    bool
    ManageSaleOpFrame::createUpdateEndTimeRequest(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager,
                                                  Database &db) {
        auto reference = getUpdateSaleEndTimeRequestReference();
        auto const referencePtr = xdr::pointer<string64>(new string64(reference));
        auto requestHelper = ReviewableRequestHelper::Instance();
        if (requestHelper->isReferenceExist(db, getSourceID(), reference)) {
            innerResult().code(ManageSaleResultCode::UPDATE_END_TIME_REQUEST_ALREADY_EXISTS);
            return false;
        }

        auto requestFrame = ReviewableRequestFrame::createNew(delta, getSourceID(), app.getMasterID(), referencePtr,
                                                              ledgerManager.getCloseTime());
        auto &requestEntry = requestFrame->getRequestEntry();
        requestEntry.body.type(ReviewableRequestType::UPDATE_SALE_END_TIME);
        requestEntry.body.updateSaleEndTimeRequest().saleID = mManageSaleOp.saleID;
        requestEntry.body.updateSaleEndTimeRequest().newEndTime = mManageSaleOp.data.updateSaleEndTimeData().newEndTime;

        requestFrame->recalculateHashRejectReason();

        requestHelper->storeAdd(delta, db, requestFrame->mEntry);

        innerResult().code(ManageSaleResultCode::SUCCESS);
        innerResult().success().response.action(ManageSaleAction::CREATE_UPDATE_END_TIME_REQUEST);
        innerResult().success().response.updateEndTimeRequestID() = requestFrame->getRequestID();

        trySetFulfilled(ledgerManager, false);

        return true;
    }

    bool ManageSaleOpFrame::amendUpdateEndTimeRequest(LedgerManager &lm, Database &db, LedgerDelta &delta) {
        auto requestFrame = ReviewableRequestHelper::Instance()->loadRequest(
                mManageSaleOp.data.updateSaleEndTimeData().requestID, getSourceID(),
                ReviewableRequestType::UPDATE_SALE_END_TIME, db, &delta);

        if (!requestFrame) {
            innerResult().code(ManageSaleResultCode::UPDATE_END_TIME_REQUEST_NOT_FOUND);
            return false;
        }

        auto &requestEntry = requestFrame->getRequestEntry();
        requestEntry.body.updateSaleEndTimeRequest().newEndTime = mManageSaleOp.data.updateSaleEndTimeData().newEndTime;

        requestFrame->recalculateHashRejectReason();
        ReviewableRequestHelper::Instance()->storeChange(delta, db, requestFrame->mEntry);

        innerResult().code(ManageSaleResultCode::SUCCESS);
        innerResult().success().response.action(ManageSaleAction::CREATE_UPDATE_END_TIME_REQUEST);
        innerResult().success().response.updateEndTimeRequestID() = requestFrame->getRequestID();

        trySetFulfilled(lm, false);

        return true;
    }

    void ManageSaleOpFrame::cancelSale(SaleFrame::pointer sale, LedgerDelta &delta, Database &db, LedgerManager &lm) {
        for (auto &saleQuoteAsset : sale->getSaleEntry().quoteAssets) {
            cancelAllOffersForQuoteAsset(sale, saleQuoteAsset, delta, db);
        }

        if (lm.shouldUse(LedgerVersion::USE_SALE_ANTE)) {
            deleteAllAntesForSale(sale->getID(), delta, db);
        }

        AccountManager::unlockPendingIssuanceForSale(sale, delta, db, lm);
        SaleHelper::Instance()->storeDelete(delta, db, sale->getKey());
    }

    void ManageSaleOpFrame::cancelAllOffersForQuoteAsset(SaleFrame::pointer sale, SaleQuoteAsset const &saleQuoteAsset,
                                                         LedgerDelta &delta, Database &db) {
        auto orderBookID = sale->getID();
        const auto offersToCancel = OfferHelper::Instance()->loadOffersWithFilters(sale->getBaseAsset(),
                                                                                   saleQuoteAsset.quoteAsset,
                                                                                   &orderBookID, nullptr, db);
        OfferManager::deleteOffers(offersToCancel, db, delta);
    }

    void ManageSaleOpFrame::deleteAllAntesForSale(uint64_t saleID, LedgerDelta &delta, Database &db) {
        auto saleAntes = SaleAnteHelper::Instance()->loadSaleAntesForSale(saleID, db);
        for (auto &saleAnte : saleAntes) {
            auto participantBalanceFrame = BalanceHelper::Instance()->mustLoadBalance(
                    saleAnte->getParticipantBalanceID(),
                    db, &delta);
            if (!participantBalanceFrame->unlock(saleAnte->getAmount())) {
                std::string strParticipantBalanceID = PubKeyUtils::toStrKey(saleAnte->getParticipantBalanceID());
                CLOG(ERROR, Logging::OPERATION_LOGGER)
                        << "Failed to unlock locked amount for sale ante with sale id: " << saleAnte->getSaleID()
                        << " and participant balance id: " << strParticipantBalanceID;
                throw std::runtime_error("Failed to unlock locked amount for sale ante");
            }

            EntryHelperProvider::storeChangeEntry(delta, db, participantBalanceFrame->mEntry);
            EntryHelperProvider::storeDeleteEntry(delta, db, saleAnte->getKey());
        }
    }

    bool ManageSaleOpFrame::createPromotionUpdateRequest(Application &app, LedgerDelta &delta, Database &db,
                                                         SaleState saleState) {
        auto &ledgerManager = app.getLedgerManager();

        if (!ManageSaleOpFrame::isSaleStateValid(ledgerManager, saleState)) {
            innerResult().code(ManageSaleResultCode::INVALID_SALE_STATE);
            return false;
        }

        auto reference = getPromotionUpdateRequestReference();
        auto const referencePtr = xdr::pointer<string64>(new string64(reference));
        auto requestHelper = ReviewableRequestHelper::Instance();
        if (requestHelper->isReferenceExist(db, getSourceID(), reference)) {
            innerResult().code(ManageSaleResultCode::PROMOTION_UPDATE_REQUEST_ALREADY_EXISTS);
            return false;
        }

        auto requestFrame = ReviewableRequestFrame::createNew(delta, getSourceID(), app.getMasterID(),
                                                              referencePtr, ledgerManager.getCloseTime());

        auto &requestEntry = requestFrame->getRequestEntry();
        requestEntry.body.type(ReviewableRequestType::UPDATE_PROMOTION);
        requestEntry.body.promotionUpdateRequest().promotionID = mManageSaleOp.saleID;
        requestEntry.body.promotionUpdateRequest().newPromotionData = mManageSaleOp.data.promotionUpdateData().newPromotionData;

        requestFrame->recalculateHashRejectReason();

        requestHelper->storeAdd(delta, db, requestFrame->mEntry);

        innerResult().code(ManageSaleResultCode::SUCCESS);
        innerResult().success().response.action(ManageSaleAction::CREATE_PROMOTION_UPDATE_REQUEST);
        innerResult().success().response.promotionUpdateRequestID() = requestFrame->getRequestID();

        if (mSourceAccount->getAccountType() == AccountType::MASTER) {
            tryAutoApprove(app, db, delta, requestFrame);
            return true;
        }

        trySetFulfilled(ledgerManager, false);

        return true;
    }

    void ManageSaleOpFrame::tryAutoApprove(Application &app, Database &db, LedgerDelta &delta,
                                           ReviewableRequestFrame::pointer requestFrame) {
        auto &lm = app.getLedgerManager();

        if (!lm.shouldUse(LedgerVersion::ALLOW_TO_UPDATE_VOTING_SALES_AS_PROMOTION)) {
            return;
        }

        auto result = ReviewRequestHelper::tryApproveRequest(mParentTx, app, lm,
                                                             delta, requestFrame);
        if (result != ReviewRequestResultCode::SUCCESS) {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: tryApproveRequest expected to be success, "
                                                      "but was: " << xdr::xdr_to_string(result);
            throw std::runtime_error("Unexpected state: tryApproveRequest expected to be success");
        }

        trySetFulfilled(lm, true);
    }

    bool ManageSaleOpFrame::amendPromotionUpdateRequest(LedgerManager &lm, Database &db, LedgerDelta &delta) {
        auto requestFrame = ReviewableRequestHelper::Instance()->loadRequest(
                mManageSaleOp.data.promotionUpdateData().requestID, getSourceID(),
                ReviewableRequestType::UPDATE_PROMOTION, db, &delta);

        if (!requestFrame) {
            innerResult().code(ManageSaleResultCode::PROMOTION_UPDATE_REQUEST_NOT_FOUND);
            return false;
        }

        auto &requestEntry = requestFrame->getRequestEntry();
        requestEntry.body.promotionUpdateRequest().newPromotionData = mManageSaleOp.data.promotionUpdateData().newPromotionData;

        requestFrame->recalculateHashRejectReason();
        ReviewableRequestHelper::Instance()->storeChange(delta, db, requestFrame->mEntry);

        innerResult().code(ManageSaleResultCode::SUCCESS);
        innerResult().success().response.action(ManageSaleAction::CREATE_PROMOTION_UPDATE_REQUEST);
        innerResult().success().response.promotionUpdateRequestID() = requestFrame->getRequestID();

        trySetFulfilled(lm, false);

        return true;
    }

    bool ManageSaleOpFrame::doApply(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager) {
        Database &db = app.getDatabase();

        auto saleFrame = getSourceAccount().getAccountType() == AccountType::MASTER
                         ? SaleHelper::Instance()->loadSale(mManageSaleOp.saleID, db, &delta)
                         : SaleHelper::Instance()->loadSale(mManageSaleOp.saleID, getSourceID(), db, &delta);

        if (!saleFrame) {
            innerResult().code(ManageSaleResultCode::SALE_NOT_FOUND);
            return false;
        }

        switch (mManageSaleOp.data.action()) {
            case ManageSaleAction::CREATE_UPDATE_DETAILS_REQUEST: {
                if (mManageSaleOp.data.updateSaleDetailsData().requestID != 0) {
                    return amendUpdateSaleDetailsRequest(ledgerManager, db, delta);
                }
                return createUpdateSaleDetailsRequest(app, delta, ledgerManager, db);
            }
            case ManageSaleAction::CANCEL: {
                cancelSale(saleFrame, delta, db, ledgerManager);
                innerResult().code(ManageSaleResultCode::SUCCESS);
                innerResult().success().response.action(ManageSaleAction::CANCEL);
                break;
            }
            case ManageSaleAction::SET_STATE: {
                return setSaleState(saleFrame, app, delta, ledgerManager, db);
            }
            case ManageSaleAction::CREATE_UPDATE_END_TIME_REQUEST: {
                uint64_t newEndTime = mManageSaleOp.data.updateSaleEndTimeData().newEndTime;
                if (!saleFrame->isEndTimeValid(newEndTime, ledgerManager.getCloseTime())) {
                    innerResult().code(ManageSaleResultCode::INVALID_NEW_END_TIME);
                    return false;
                }
                if (mManageSaleOp.data.updateSaleEndTimeData().requestID != 0) {
                    return amendUpdateEndTimeRequest(ledgerManager, db, delta);
                }
                return createUpdateEndTimeRequest(app, delta, ledgerManager, db);
            }
            case ManageSaleAction::CREATE_PROMOTION_UPDATE_REQUEST: {
                if (mManageSaleOp.data.promotionUpdateData().requestID != 0) {
                    return amendPromotionUpdateRequest(ledgerManager, db, delta);
                }
                return createPromotionUpdateRequest(app, delta, db, saleFrame->getState());
            }
            default:
                CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected action from manage sale op: "
                                                       << xdr::xdr_to_string(mManageSaleOp.data.action());
                throw std::runtime_error("Unexpected action from manage sale op");
        }

        return true;
    }

    bool ManageSaleOpFrame::isPromotionUpdateDataValid(Application &app) {
        auto saleCreationRequestValidationResult = CreateSaleCreationRequestOpFrame::doCheckValid(
                app, mManageSaleOp.data.promotionUpdateData().newPromotionData, getSourceID());

        if (saleCreationRequestValidationResult == CreateSaleCreationRequestResultCode::SUCCESS) {
            return true;
        }

        switch (saleCreationRequestValidationResult) {
            case CreateSaleCreationRequestResultCode::INVALID_ASSET_PAIR: {
                innerResult().code(ManageSaleResultCode::PROMOTION_UPDATE_REQUEST_INVALID_ASSET_PAIR);
                return false;
            }
            case CreateSaleCreationRequestResultCode::INVALID_PRICE: {
                innerResult().code(ManageSaleResultCode::PROMOTION_UPDATE_REQUEST_INVALID_PRICE);
                return false;
            }
            case CreateSaleCreationRequestResultCode::START_END_INVALID: {
                innerResult().code(ManageSaleResultCode::PROMOTION_UPDATE_REQUEST_START_END_INVALID);
                return false;
            }
            case CreateSaleCreationRequestResultCode::INVALID_CAP: {
                innerResult().code(ManageSaleResultCode::PROMOTION_UPDATE_REQUEST_INVALID_CAP);
                return false;
            }
            case CreateSaleCreationRequestResultCode::INVALID_DETAILS: {
                innerResult().code(ManageSaleResultCode::PROMOTION_UPDATE_REQUEST_INVALID_DETAILS);
                return false;
            }
            default: {
                CLOG(ERROR, Logging::OPERATION_LOGGER)
                        << "Unexpected result code from CreateSaleCreationRequestOpFrame::doCheckValid: "
                        << xdr::xdr_traits<CreateSaleCreationRequestResultCode>::enum_name(
                                saleCreationRequestValidationResult);
                throw std::runtime_error("Unexpected result code from CreateSaleCreationRequestOpFrame::doCheckValid");
            }
        }
    }

    bool ManageSaleOpFrame::doCheckValid(Application &app) {
        if (mManageSaleOp.saleID == 0) {
            innerResult().code(ManageSaleResultCode::SALE_NOT_FOUND);
            return false;
        }

        if (mManageSaleOp.data.action() == ManageSaleAction::CREATE_PROMOTION_UPDATE_REQUEST) {
            return isPromotionUpdateDataValid(app);
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

    void ManageSaleOpFrame::checkRequestType(ReviewableRequestFrame::pointer request,
                                             ReviewableRequestType requestType) {
        if (request->getRequestType() != requestType) {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected request type. Expected "
                                                   << xdr::xdr_traits<ReviewableRequestType>::enum_name(requestType)
                                                   << " but got "
                                                   << xdr::xdr_traits<ReviewableRequestType>::enum_name(
                                                           request->getRequestType());
            throw std::invalid_argument("Unexpected request type");
        }
    }

    bool ManageSaleOpFrame::isSaleStateValid(LedgerManager &lm, SaleState saleState) {
        if (!lm.shouldUse(LedgerVersion::ALLOW_TO_UPDATE_VOTING_SALES_AS_PROMOTION)) {
            return saleState == SaleState::PROMOTION;
        }

        if (saleState != SaleState::PROMOTION && saleState != SaleState::VOTING) {
            return false;
        }

        return true;
    }

    void ManageSaleOpFrame::trySetFulfilled(LedgerManager &lm, bool fulfilled) {
        if (!lm.shouldUse(LedgerVersion::ALLOW_TO_UPDATE_VOTING_SALES_AS_PROMOTION)) {
            return;
        }

        innerResult().success().ext.v(LedgerVersion::ALLOW_TO_UPDATE_VOTING_SALES_AS_PROMOTION);
        innerResult().success().ext.fulfilled() = fulfilled;
    }
}
