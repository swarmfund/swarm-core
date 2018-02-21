#include "transactions/ManageExternalSystemIDProviderOpFrame.h"
#include "ledger/LedgerDelta.h"
#include "ledger/AccountHelper.h"
#include "ledger/ExternalSystemAccountIDProvider.h"
#include "ledger/ExternalSystemAccountIDProviderHelper.h"
#include "main/Application.h"

namespace stellar
{
using namespace std;
using xdr::operator==;

    std::unordered_map<AccountID, CounterpartyDetails>
    ManageExternalSystemProviderOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
    {
        return {};
    }

    SourceDetails
    ManageExternalSystemProviderOpFrame::getSourceAccountDetails(
            std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails, int32_t ledgerVersion) const
    {
        vector<AccountType> allowedSourceAccounts;
        allowedSourceAccounts = { AccountType::MASTER };
        return SourceDetails(allowedSourceAccounts, mSourceAccount->getHighThreshold(),
                             static_cast<int32_t>(SignerType::EXTERNAL_SYSTEM_ACCOUNT_ID_POOL_MANAGER));
    }

    ManageExternalSystemProviderOpFrame::ManageExternalSystemProviderOpFrame(Operation const &op, OperationResult &res,
                                                                             TransactionFrame &parentTx)
            : OperationFrame(op, res, parentTx),
              mManageExternalSystemIdProviderOp(mOperation.body.manageExternalSystemIdProviderOp())
    {
    }

    bool
    ManageExternalSystemProviderOpFrame::doApply(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager)
    {
        Database& db = ledgerManager.getDatabase();

        auto providerFrame = ExternalSystemAccountIDProviderHelper::Instance()->load(
                mManageExternalSystemIdProviderOp.externalSystemType, mManageExternalSystemIdProviderOp.data, db, &delta);

        if (!!providerFrame)
        {
            innerResult().code(ManageExternalSystemIdProviderResultCode::ALREADY_EXISTS);
            return false;
        }

        auto newProviderID = delta.getHeaderFrame().generateID(LedgerEntryType::EXTERNAL_SYSTEM_ACCOUNT_ID_PROVIDER);
        providerFrame = ExternalSystemAccountIDProviderFrame::createNew(newProviderID,
                mManageExternalSystemIdProviderOp.externalSystemType, mManageExternalSystemIdProviderOp.data);

        ExternalSystemAccountIDProviderHelper::Instance()->storeAdd(delta, db, providerFrame->mEntry);
        innerResult().code(ManageExternalSystemIdProviderResultCode::SUCCESS);
        innerResult().success().providerID = newProviderID;
        return true;
    }

    bool
    ManageExternalSystemProviderOpFrame::doCheckValid(Application &app)
    {
        if (!isValidEnumValue(mManageExternalSystemIdProviderOp.externalSystemType))
        {
            innerResult().code(ManageExternalSystemIdProviderResultCode::MALFORMED);
            return false;
        }
        if (mManageExternalSystemIdProviderOp.data.empty())
        {
            innerResult().code(ManageExternalSystemIdProviderResultCode::MALFORMED);
            return false;
        }

        return true;
    }
}
