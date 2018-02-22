#include "transactions/BindExternalSystemAccountIdOpFrame.h"
#include "ledger/LedgerDelta.h"
#include "ledger/AccountHelper.h"
#include "ledger/ExternalSystemAccountID.h"
#include "ledger/ExternalSystemAccountIDHelper.h"
#include "ledger/ExternalSystemAccountIDPoolEntryHelper.h"
#include "database/Database.h"
#include "main/Application.h"

namespace stellar
{

using namespace std;
using xdr::operator==;

std::unordered_map<AccountID , CounterpartyDetails>
BindExternalSystemAccountIdOpFrame::getCounterpartyDetails(Database &db, LedgerDelta *delta) const
{
    // no counterparties
    return std::unordered_map<AccountID, CounterpartyDetails>();
}

SourceDetails
BindExternalSystemAccountIdOpFrame::getSourceAccountDetails(
        std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails, int32_t ledgerVersion) const
{
    std::vector<AccountType> allowedSourceAccounts;
    allowedSourceAccounts = { AccountType::GENERAL, AccountType::NOT_VERIFIED };
    return SourceDetails(allowedSourceAccounts, mSourceAccount->getLowThreshold(),
                         static_cast<int32_t >(SignerType::BALANCE_MANAGER));
}

BindExternalSystemAccountIdOpFrame::BindExternalSystemAccountIdOpFrame(Operation const &op, OperationResult &res,
                                                                       TransactionFrame &parentTx)
        : OperationFrame(op, res, parentTx),
          mBindExternalSystemAccountId(mOperation.body.bindExternalSystemAccountIdOp())
{
}

bool
BindExternalSystemAccountIdOpFrame::doApply(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager)
{
    Database& db = app.getDatabase();

    auto externalSystemAccountIDPoolEntryHelper = ExternalSystemAccountIDPoolEntryHelper::Instance();

    bool alreadyHas = externalSystemAccountIDPoolEntryHelper->existsForAccount(db,
                                                                               mBindExternalSystemAccountId.externalSystemType,
                                                                               mSourceAccount->getID());
    if (alreadyHas)
    {
        innerResult().code(BindExternalSystemAccountIdResultCode::ALREADY_HAS);
        return false;
    }

    auto availablePoolEntries = externalSystemAccountIDPoolEntryHelper->loadAvailablePoolEntries(db,
                                                                     mBindExternalSystemAccountId.externalSystemType);
    if (availablePoolEntries.empty())
    {
        innerResult().code(BindExternalSystemAccountIdResultCode::NO_AVAILABLE_ID);
        return false;
    }

    auto poolEntryToBindFrame = availablePoolEntries.front();
    ExternalSystemAccountIDPoolEntry& poolEntryToBind = poolEntryToBindFrame->getExternalSystemAccountIDPoolEntry();
    poolEntryToBind.accountID.activate() = mSourceAccount->getID();
    poolEntryToBind.expiresAt = ledgerManager.getCloseTime() + (24 * 60 * 60);

    auto externalSystemAccountIDFrame = ExternalSystemAccountIDFrame::createNew(mSourceAccount->getID(),
                                                                                mBindExternalSystemAccountId.externalSystemType,
                                                                                poolEntryToBind.data);

    externalSystemAccountIDPoolEntryHelper->storeChange(delta, db, poolEntryToBindFrame->mEntry);
    ExternalSystemAccountIDHelper::Instance()->storeAdd(delta, db, externalSystemAccountIDFrame->mEntry);
    innerResult().code(BindExternalSystemAccountIdResultCode::SUCCESS);
    innerResult().success().data = poolEntryToBind.data;
    return true;
}

bool
BindExternalSystemAccountIdOpFrame::doCheckValid(Application &app)
{
    if (!isValidEnumValue(mBindExternalSystemAccountId.externalSystemType))
    {
        innerResult().code(BindExternalSystemAccountIdResultCode::MALFORMED);
        return false;
    }

    return true;
}
}
