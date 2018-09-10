#include <lib/xdrpp/xdrpp/printer.h>
#include <transactions/review_request/ReviewIssuanceCreationRequestOpFrame.h>
#include <main/Application.h>
#include <transactions/review_request/ReviewRequestHelper.h>
#include <ledger/ExternalSystemAccountIDHelperLegacy.h>
#include "ledger/ExternalSystemAccountIDPoolEntryHelperLegacy.h"
#include "DeleteExternalSystemAccountIDPoolEntryOpFrame.h"

namespace stellar
{
using namespace std;

DeleteExternalSystemAccountIDPoolEntryOpFrame::DeleteExternalSystemAccountIDPoolEntryOpFrame(Operation const &op,
                                                                                             OperationResult &res,
                                                                                             TransactionFrame &parentTx)
        : ManageExternalSystemAccountIdPoolEntryOpFrame(op, res, parentTx),
          mInput(mManageExternalSystemAccountIdPoolEntryOp.actionInput.deleteExternalSystemAccountIdPoolEntryActionInput())
{
}

bool
DeleteExternalSystemAccountIDPoolEntryOpFrame::doApply(Application &app, LedgerDelta &delta,
                                                           LedgerManager &ledgerManager)
{
    innerResult().code(ManageExternalSystemAccountIdPoolEntryResultCode::SUCCESS);

    Database& db = ledgerManager.getDatabase();
    auto poolEntryHelper = ExternalSystemAccountIDPoolEntryHelperLegacy::Instance();

    auto poolEntryToDeleteFrame = poolEntryHelper->load(mInput.poolEntryID, db, &delta);

    if (!poolEntryToDeleteFrame)
    {
        innerResult().code(ManageExternalSystemAccountIdPoolEntryResultCode::NOT_FOUND);
        return false;
    }

    if (poolEntryToDeleteFrame->getExternalSystemAccountIDPoolEntry().expiresAt > ledgerManager.getCloseTime())
    {
        poolEntryToDeleteFrame->markAsDeleted();
        poolEntryHelper->storeChange(delta, db, poolEntryToDeleteFrame->mEntry);
        return true;
    }

    ExternalSystemAccountIDPoolEntry& poolEntryToDelete = poolEntryToDeleteFrame->getExternalSystemAccountIDPoolEntry();

    if (!!poolEntryToDelete.accountID)
    {
        auto externalSystemAccountIDHelper = ExternalSystemAccountIDHelperLegacy::Instance();
        auto existingExternalSystemAccountIDFrame = externalSystemAccountIDHelper->load(*poolEntryToDelete.accountID,
                                                                                   poolEntryToDelete.externalSystemType,
                                                                                   db, &delta);

        if (!existingExternalSystemAccountIDFrame)
        {
            auto accIDStr = PubKeyUtils::toStrKey(*poolEntryToDelete.accountID);
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Failed to load existing external system account id for account id:"
                                                   << accIDStr;
            throw runtime_error("Unexpected state: external system account id expected to exist");
        }

        externalSystemAccountIDHelper->storeDelete(delta, db, existingExternalSystemAccountIDFrame->getKey());
    }

    poolEntryHelper->storeDelete(delta, db, poolEntryToDeleteFrame->getKey());

    return true;
}

bool
DeleteExternalSystemAccountIDPoolEntryOpFrame::doCheckValid(Application &app)
{
    return true;
}

}