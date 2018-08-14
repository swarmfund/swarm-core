#include "transactions/BindExternalSystemAccountIdOpFrame.h"
#include "ManageKeyValueOpFrame.h"
#include "database/Database.h"
#include "ledger/AccountHelper.h"
#include "ledger/ExternalSystemAccountID.h"
#include "ledger/ExternalSystemAccountIDHelper.h"
#include "ledger/ExternalSystemAccountIDPoolEntryHelper.h"
#include "ledger/KeyValueHelper.h"
#include "ledger/LedgerDelta.h"
#include "ledger/StorageHelper.h"
#include "main/Application.h"

namespace stellar
{

using namespace std;
using xdr::operator==;

std::unordered_map<AccountID, CounterpartyDetails>
BindExternalSystemAccountIdOpFrame::getCounterpartyDetails(
    Database& db, LedgerDelta* delta) const
{
    // no counterparties
    return std::unordered_map<AccountID, CounterpartyDetails>();
}

SourceDetails
BindExternalSystemAccountIdOpFrame::getSourceAccountDetails(
    std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
    int32_t ledgerVersion) const
{
    std::vector<AccountType> allowedSourceAccounts;
    allowedSourceAccounts = { AccountType::GENERAL, AccountType::NOT_VERIFIED, AccountType::SYNDICATE,
                              AccountType::ACCREDITED_INVESTOR, AccountType::INSTITUTIONAL_INVESTOR,
                              AccountType::VERIFIED};
    return SourceDetails(allowedSourceAccounts, mSourceAccount->getLowThreshold(),
                         static_cast<int32_t >(SignerType::BALANCE_MANAGER),
                         static_cast<uint32_t>(BlockReasons::WITHDRAWAL));
}

BindExternalSystemAccountIdOpFrame::BindExternalSystemAccountIdOpFrame(
    Operation const& op, OperationResult& res, TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mBindExternalSystemAccountId(
          mOperation.body.bindExternalSystemAccountIdOp())
{
}

bool
BindExternalSystemAccountIdOpFrame::doApply(Application& app,
                                            StorageHelper& storageHelper,
                                            LedgerManager& ledgerManager)
{
    Database& db = storageHelper.getDatabase();
    LedgerDelta& delta = storageHelper.getLedgerDelta();

    auto& externalSystemAccountIDHelper =
        storageHelper.getExternalSystemAccountIDHelper();
    auto& externalSystemAccountIDPoolEntryHelper =
        storageHelper.getExternalSystemAccountIDPoolEntryHelper();

    auto existingPoolEntryFrame = externalSystemAccountIDPoolEntryHelper.load(
        mBindExternalSystemAccountId.externalSystemType,
        mSourceAccount->getID());
    if (!!existingPoolEntryFrame)
    {
        existingPoolEntryFrame->getExternalSystemAccountIDPoolEntry()
            .expiresAt = ledgerManager.getCloseTime() + dayInSeconds;
        externalSystemAccountIDPoolEntryHelper.storeChange(
            existingPoolEntryFrame->mEntry);
        innerResult().code(BindExternalSystemAccountIdResultCode::SUCCESS);
        innerResult().success().data =
            existingPoolEntryFrame->getExternalSystemAccountIDPoolEntry().data;
        return true;
    }

    auto poolEntryToBindFrame =
        externalSystemAccountIDPoolEntryHelper.loadAvailablePoolEntry(
            ledgerManager, mBindExternalSystemAccountId.externalSystemType);
    if (!poolEntryToBindFrame)
    {
        innerResult().code(
            BindExternalSystemAccountIdResultCode::NO_AVAILABLE_ID);
        return false;
    }

    ExternalSystemAccountIDPoolEntry& poolEntryToBind =
        poolEntryToBindFrame->getExternalSystemAccountIDPoolEntry();

    if (poolEntryToBind.accountID)
    {
        auto existingExternalSystemAccountIDFrame =
            externalSystemAccountIDHelper.load(
                *poolEntryToBind.accountID,
                mBindExternalSystemAccountId.externalSystemType);
        if (!existingExternalSystemAccountIDFrame)
        {
            auto accIDStr = PubKeyUtils::toStrKey(*poolEntryToBind.accountID);
            CLOG(ERROR, Logging::OPERATION_LOGGER)
                << "Failed to load existing external system account id for "
                   "account id:"
                << accIDStr;
            throw runtime_error("Unexpected state: external system account id "
                                "expected to exist");
        }

        externalSystemAccountIDHelper.storeDelete(
            existingExternalSystemAccountIDFrame->getKey());
    }

    poolEntryToBind.accountID.activate() = mSourceAccount->getID();

    int expiresAt =
        getExpiresAt(storageHelper, ledgerManager,
                     mBindExternalSystemAccountId.externalSystemType);
    poolEntryToBind.expiresAt = ledgerManager.getCloseTime() + expiresAt;

    poolEntryToBind.bindedAt = ledgerManager.getCloseTime();

    auto externalSystemAccountIDFrame = ExternalSystemAccountIDFrame::createNew(
        mSourceAccount->getID(),
        mBindExternalSystemAccountId.externalSystemType, poolEntryToBind.data);

    externalSystemAccountIDPoolEntryHelper.storeChange(
        poolEntryToBindFrame->mEntry);
    externalSystemAccountIDHelper.storeAdd(
        externalSystemAccountIDFrame->mEntry);
    innerResult().code(BindExternalSystemAccountIdResultCode::SUCCESS);
    innerResult().success().data = poolEntryToBind.data;
    return true;
}

bool
BindExternalSystemAccountIdOpFrame::doCheckValid(Application& app)
{
    if (ExternalSystemAccountIDPoolEntryFrame::isAutoGenerated(
            mBindExternalSystemAccountId.externalSystemType))
    {
        innerResult().code(BindExternalSystemAccountIdResultCode::
                               AUTO_GENERATED_TYPE_NOT_ALLOWED);
        return false;
    }

    return true;
}

int
BindExternalSystemAccountIdOpFrame::getExpiresAt(StorageHelper& storageHelper,
                                                 LedgerManager& ledgerManager,
                                                 int32 externalSystemType)
{
    if (!ledgerManager.shouldUse(
            LedgerVersion::KEY_VALUE_POOL_ENTRY_EXPIRES_AT))
    {
        return dayInSeconds;
    }

    auto key = ManageKeyValueOpFrame::makeExternalSystemExpirationPeriodKey(
        externalSystemType);

    auto kvEntry = storageHelper.getKeyValueHelper().loadKeyValue(key);

    if (!kvEntry)
    {
        return dayInSeconds;
    }

    if (kvEntry.get()->getKeyValue().value.type() != KeyValueEntryType::UINT32)
    {
        CLOG(WARNING, "BindExternalSystemAccountId")
            << "KeyValueEntryType: "
            << to_string(static_cast<int32>(
                   kvEntry.get()->getKeyValue().value.type()));
        return dayInSeconds;
    }

    return kvEntry.get()->getKeyValue().value.ui32Value();
}

} // namespace stellar
