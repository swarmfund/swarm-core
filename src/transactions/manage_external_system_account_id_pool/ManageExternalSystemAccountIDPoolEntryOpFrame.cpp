#include "ManageExternalSystemAccountIDPoolEntryOpFrame.h"
#include "CreateExternalSystemAccountIDPoolEntryOpFrame.h"
#include "DeleteExternalSystemAccountIDPoolEntryOpFrame.h"
#include "ledger/LedgerDelta.h"
#include "ledger/AccountHelper.h"
#include "main/Application.h"

namespace stellar
{
using namespace std;
using xdr::operator==;

    std::unordered_map<AccountID, CounterpartyDetails>
    ManageExternalSystemAccountIdPoolEntryOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
    {
        // no counterparties
        return std::unordered_map<AccountID, CounterpartyDetails>();
    }

    SourceDetails
    ManageExternalSystemAccountIdPoolEntryOpFrame::getSourceAccountDetails(
            std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails, int32_t ledgerVersion) const
    {
        vector<AccountType> allowedSourceAccounts;
        allowedSourceAccounts = { AccountType::MASTER };
        return SourceDetails(allowedSourceAccounts, mSourceAccount->getHighThreshold(),
                             static_cast<int32_t>(SignerType::EXTERNAL_SYSTEM_ACCOUNT_ID_POOL_MANAGER));
    }

    ManageExternalSystemAccountIdPoolEntryOpFrame::ManageExternalSystemAccountIdPoolEntryOpFrame(Operation const &op,
                                                                                                 OperationResult &res,
                                                                             TransactionFrame &parentTx)
            : OperationFrame(op, res, parentTx),
              mManageExternalSystemAccountIdPoolEntryOp(mOperation.body.manageExternalSystemAccountIdPoolEntryOp())
    {
    }

    ManageExternalSystemAccountIdPoolEntryOpFrame * ManageExternalSystemAccountIdPoolEntryOpFrame::makeHelper(
            Operation const &op, OperationResult &res, TransactionFrame &parentTx)
    {
        switch (op.body.manageExternalSystemAccountIdPoolEntryOp().actionInput.action()) {
            case ManageExternalSystemAccountIdPoolEntryAction::CREATE:
                return new CreateExternalSystemAccountIDPoolEntryOpFrame(op, res, parentTx);
            case ManageExternalSystemAccountIdPoolEntryAction::REMOVE:
                return new DeleteExternalSystemAccountIDPoolEntryOpFrame(op, res, parentTx);
            default:
                throw runtime_error("Unexpected action in manage external system account id pool entry op");
        }
    }
}
