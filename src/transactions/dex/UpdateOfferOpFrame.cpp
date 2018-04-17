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
        const auto opRes = executeManageOfferOp(app, delta, ledgerManager, manageOfferOp);
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
        const auto opRes = executeManageOfferOp(app, delta, ledgerManager, manageOfferOp);
        const auto createOfferResult = ManageOfferOpFrame::getInnerResult(opRes);
        return createOfferResult;
    }

    OperationResult
    UpdateOfferOpFrame::executeManageOfferOp(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager,
                                             ManageOfferOp const &manageOfferOp) {
        Operation op;
        op.sourceAccount.activate() = mSourceAccount->getID();
        op.body.type(OperationType::MANAGE_OFFER);
        op.body.manageOfferOp() = manageOfferOp;

        OperationResult opRes;
        opRes.code(OperationResultCode::opINNER);
        opRes.tr().type(OperationType::MANAGE_OFFER);

        auto manageOfferOpFrame = shared_ptr<ManageOfferOpFrame>(ManageOfferOpFrame::make(op, opRes, mParentTx));

        manageOfferOpFrame->setSourceAccountPtr(mSourceAccount);

        if (!manageOfferOpFrame->doCheckValid(app) || !manageOfferOpFrame->doApply(app, delta, ledgerManager)) {
            CLOG(ERROR, Logging::OPERATION_LOGGER)
                    << "Unexpected state: failed to execute manage offer operation on update offer: "
                    << manageOfferOp.offerID
                    << manageOfferOpFrame->getInnerResultCodeAsStr();
            throw runtime_error("Unexpected state: failed to execute manage offer operation on update offer");
        }

        return opRes;
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
