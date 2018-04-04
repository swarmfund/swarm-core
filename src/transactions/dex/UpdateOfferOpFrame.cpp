#include <ledger/OfferHelper.h>
#include "main/Application.h"
#include "database/Database.h"
#include "UpdateOfferOpFrame.h"
#include "DeleteOfferOpFrame.h"
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

        auto manageOfferOpFrame = ManageOfferOpFrame::make(op, opRes, mParentTx);

        manageOfferOpFrame->setSourceAccountPtr(mSourceAccount);
        manageOfferOpFrame->doCheckValid(app);
        manageOfferOpFrame->doApply(app, delta, ledgerManager);

        const auto manageOfferResultCode = ManageOfferOpFrame::getInnerCode(opRes);

        return manageOfferResultCode;
    }

    bool
    UpdateOfferOpFrame::doApply(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager) {
        auto& db = app.getDatabase();

        const auto offer = OfferHelper::Instance()->loadOffer(getSourceID(), mManageOffer.offerID, mManageOffer.orderBookID, db, &delta);
        if (!offer)
        {
            innerResult().code(ManageOfferResultCode::OFFER_NOT_FOUND);
            return false;
        }

        auto tryDeleteOfferResultCode = tryDeleteOffer(app, delta, ledgerManager);

        if (tryDeleteOfferResultCode != ManageOfferResultCode::SUCCESS) {
            innerResult().code(tryDeleteOfferResultCode);
            return false;
        }


    }

    bool
    UpdateOfferOpFrame::doCheckValid(Application &app) {
        return true;
    }
}
