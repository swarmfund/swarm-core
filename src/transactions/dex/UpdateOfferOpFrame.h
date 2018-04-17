#pragma once

#include "ManageOfferOpFrame.h"
#include "OfferExchange.h"

namespace stellar {
    class UpdateOfferOpFrame : public ManageOfferOpFrame {
        ManageOfferResultCode tryDeleteOffer(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager);

        ManageOfferResult tryCreateOffer(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager);

        OperationResult executeManageOfferOp(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager,
                                             ManageOfferOp const& manageOfferOp);

    public:
        UpdateOfferOpFrame(Operation const &op, OperationResult &res, TransactionFrame &parentTx);

        bool doApply(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager) override;

        bool doCheckValid(Application &app) override;
    };
}