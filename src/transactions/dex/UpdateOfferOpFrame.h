#pragma once

#include "ManageOfferOpFrame.h"
#include "OfferExchange.h"

namespace stellar {
    class UpdateOfferOpFrame : public ManageOfferOpFrame {
        ManageOfferResultCode tryDeleteOffer(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager);

        ManageOfferResultCode tryCreateOffer();

    public:
        UpdateOfferOpFrame(Operation const &op, OperationResult &res, TransactionFrame &parentTx);

        bool doApply(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager) override;

        bool doCheckValid(Application &app) override;
    };
}