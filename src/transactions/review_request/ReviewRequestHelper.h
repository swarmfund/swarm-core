#ifndef STELLAR_REVIEWREQUESTHELPER_H
#define STELLAR_REVIEWREQUESTHELPER_H

#include <ledger/ReviewableRequestFrame.h>
#include <transactions/TransactionFrame.h>
#include "main/Application.h"
#include "ledger/LedgerDelta.h"
#include "ledger/LedgerManager.h"
#include "database/Database.h"
#include <lib/soci/src/core/transaction.h>
#include <lib/xdrpp/xdrpp/printer.h>

namespace stellar {

class ReviewRequestHelper
{
private:
    Application& mApp;
    LedgerDelta& mDelta;
    LedgerManager& mLedgerManager;
    ReviewableRequestFrame::pointer mRequest;

    std::pair<bool, ReviewRequestResult> tryReviewRequest(TransactionFrame& parentTx);

public:
    ReviewRequestHelper(Application &app, LedgerManager &ledgerManager, LedgerDelta &delta,
                            ReviewableRequestFrame::pointer reviewableRequest);

    ReviewRequestResultCode tryApproveRequest(TransactionFrame &parentTx);
};

}

#endif //STELLAR_REVIEWREQUESTHELPER_H
