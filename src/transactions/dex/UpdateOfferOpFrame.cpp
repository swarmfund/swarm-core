#include <ledger/OfferHelper.h>
#include "main/Application.h"
#include "database/Database.h"
#include "UpdateOfferOpFrame.h"
#include "DeleteOfferOpFrame.h"
#include "CreateOfferOpFrame.h"
#include "OfferManager.h"

namespace stellar {
    using namespace std;
    using xdr::operator==;

    UpdateOfferOpFrame::UpdateOfferOpFrame(const Operation &op, OperationResult &res,
                                           TransactionFrame &parentTx) : ManageOfferOpFrame(op, res, parentTx) {
    }

    ManageOfferResultCode
    UpdateOfferOpFrame::tryDeleteOffer(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager) {
        auto manageOfferOp = OfferManager::buildManageOfferOp(mManageOffer.baseBalance, mManageOffer.quoteBalance,
                                                              mManageOffer.isBuy, 0, mManageOffer.price,
                                                              mManageOffer.fee, mManageOffer.offerID,
                                                              mManageOffer.orderBookID);
        Operation op;
        op.sourceAccount.activate() = mSourceAccount->getID();
        op.body.type(OperationType::MANAGE_OFFER);
        op.body.manageOfferOp() = manageOfferOp;

        OperationResult opRes;
        opRes.code(OperationResultCode::opINNER);
        opRes.tr().type(OperationType::MANAGE_OFFER);

        auto deleteOfferOpFrame = dynamic_cast<DeleteOfferOpFrame *>(ManageOfferOpFrame::make(op, opRes, mParentTx));
        if (!deleteOfferOpFrame) {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to cast ManageOfferOpFrame to DeleteOfferOpFrame";
            throw std::runtime_error("Failed to cast ManageOfferOpFrame to DeleteOfferOpFrame");
        }

        deleteOfferOpFrame->setSourceAccountPtr(mSourceAccount);
        deleteOfferOpFrame->doCheckValid(app);
        deleteOfferOpFrame->doApply(app, delta, ledgerManager);

        const auto deleteOfferResultCode = ManageOfferOpFrame::getInnerCode(opRes);

        return deleteOfferResultCode;
    }

    ManageOfferResult UpdateOfferOpFrame::tryCreateOffer(Application &app, LedgerDelta &delta,
                                                             LedgerManager &ledgerManager) {
        auto manageOfferOp = OfferManager::buildManageOfferOp(mManageOffer.baseBalance, mManageOffer.quoteBalance,
                                                              mManageOffer.isBuy, mManageOffer.amount,
                                                              mManageOffer.price,
                                                              mManageOffer.fee, 0,
                                                              mManageOffer.orderBookID);
        Operation op;
        op.sourceAccount.activate() = mSourceAccount->getID();
        op.body.type(OperationType::MANAGE_OFFER);
        op.body.manageOfferOp() = manageOfferOp;

        OperationResult opRes;
        opRes.code(OperationResultCode::opINNER);
        opRes.tr().type(OperationType::MANAGE_OFFER);

        auto createOfferOpFrame = dynamic_cast<CreateOfferOpFrame *>(ManageOfferOpFrame::make(op, opRes, mParentTx));
        if (!createOfferOpFrame) {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to cast ManageOfferOpFrame to CreateOfferOpFrame";
            throw std::runtime_error("Failed to cast ManageOfferOpFrame to CreateOfferOpFrame");
        }

        createOfferOpFrame->setSourceAccountPtr(mSourceAccount);
        createOfferOpFrame->doCheckValid(app);
        createOfferOpFrame->doApply(app, delta, ledgerManager);

        const auto createOfferResult = ManageOfferOpFrame::getInnerResult(opRes);

        return createOfferResult;
    }

    bool
    UpdateOfferOpFrame::doApply(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager) {
        auto &db = app.getDatabase();

        const auto offerFrame = OfferHelper::Instance()->loadOffer(getSourceID(), mManageOffer.offerID,
                                                                   mManageOffer.orderBookID, db, &delta);
        if (!offerFrame) {
            innerResult().code(ManageOfferResultCode::OFFER_NOT_FOUND);
            return false;
        }

        auto tryDeleteOfferResultCode = tryDeleteOffer(app, delta, ledgerManager);

        if (tryDeleteOfferResultCode != ManageOfferResultCode::SUCCESS) {
            innerResult().code(tryDeleteOfferResultCode);
            return false;
        }

        auto tryCreateOfferResult = tryCreateOffer(app, delta, ledgerManager);

        if (tryCreateOfferResult.code() != ManageOfferResultCode::SUCCESS) {
            innerResult().code(tryCreateOfferResult.code());
            return false;
        }

        innerResult().code(ManageOfferResultCode::SUCCESS);
        innerResult().success().offer.effect(ManageOfferEffect::UPDATED);
        innerResult().success().offer.offer() = tryCreateOfferResult.success().offer.offer();
        return true;
    }

    bool
    UpdateOfferOpFrame::doCheckValid(Application &app) {
        return true;
    }
}
