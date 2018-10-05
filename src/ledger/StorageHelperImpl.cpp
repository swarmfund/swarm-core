#include "ledger/StorageHelperImpl.h"
#include "ledger/ExternalSystemAccountIDHelperImpl.h"
#include "ledger/ExternalSystemAccountIDPoolEntryHelperImpl.h"
#include "ledger/KeyValueHelperImpl.h"
#include "ledger/LedgerDeltaImpl.h"
#include "BalanceHelperImpl.h"
#include "AssetHelperImpl.h"

namespace stellar
{

StorageHelperImpl::StorageHelperImpl(Database& db, LedgerDelta* ledgerDelta)
    : mDatabase(db)
    , mLedgerDelta(ledgerDelta)
    , mTransaction(new soci::transaction(db.getSession()))
{
}

StorageHelperImpl::~StorageHelperImpl()
{
    if (mTransaction)
    {
        try
        {
            mTransaction->rollback();
        }
        catch (...)
        {
        }
    }
}

Database&
StorageHelperImpl::getDatabase()
{
    return mDatabase;
}
const Database&
StorageHelperImpl::getDatabase() const
{
    return mDatabase;
}
LedgerDelta*
StorageHelperImpl::getLedgerDelta()
{
    return mLedgerDelta;
}
const LedgerDelta*
StorageHelperImpl::getLedgerDelta() const
{
    return mLedgerDelta;
}

void
StorageHelperImpl::commit()
{
    if (mIsReleased)
    {
        throw std::runtime_error("Cannot commit a released StorageHelper.");
    }
    if (mLedgerDelta)
    {
        mLedgerDelta->commit();
    }
    if (mTransaction)
    {
        mTransaction->commit();
        mTransaction = nullptr;
    }
}
void
StorageHelperImpl::rollback()
{
    if (mIsReleased)
    {
        throw std::runtime_error("Cannot rollback a released StorageHelper.");
    }
    if (mLedgerDelta)
    {
        mLedgerDelta->rollback();
    }
    if (mTransaction)
    {
        mTransaction->rollback();
        mTransaction = nullptr;
    }
}
void
StorageHelperImpl::release()
{
    mIsReleased = true;
    if (mTransaction)
    {
        mTransaction->rollback();
        mTransaction = nullptr;
    }
}

std::unique_ptr<StorageHelper>
StorageHelperImpl::startNestedTransaction()
{
    if (!mLedgerDelta)
    {
        throw std::runtime_error("This StorageHelper has empty ledger delta and "
                                 "cannot create nested transactions.");
    }
    if (mNestedDelta && mNestedDelta->isStateActive())
    {
        throw std::runtime_error("Invalid operation: this StorageHelper "
                                 "already has an active nested StorageHelper");
    }
    mNestedDelta = std::make_unique<LedgerDeltaImpl>(*mLedgerDelta);
    return std::make_unique<StorageHelperImpl>(mDatabase, mNestedDelta.get());
}

KeyValueHelper&
StorageHelperImpl::getKeyValueHelper()
{
    if (!mKeyValueHelper)
    {
        mKeyValueHelper = std::make_unique<KeyValueHelperImpl>(*this);
    }
    return *mKeyValueHelper;
}
BalanceHelper&
StorageHelperImpl::getBalanceHelper()
{
    if (!mBalanceHelper)
    {
        mBalanceHelper = std::make_unique<BalanceHelperImpl>(*this);
    }
    return *mBalanceHelper;
}
AssetHelper&
StorageHelperImpl::getAssetHelper()
{
    if (!mAssetHelper)
    {
        mAssetHelper = std::make_unique<AssetHelperImpl>(*this);
    }
    return *mAssetHelper;
}
ExternalSystemAccountIDHelper&
StorageHelperImpl::getExternalSystemAccountIDHelper()
{
    if (!mExternalSystemAccountIDHelper)
    {
        mExternalSystemAccountIDHelper =
            std::make_unique<ExternalSystemAccountIDHelperImpl>(*this);
    }
    return *mExternalSystemAccountIDHelper;
}
ExternalSystemAccountIDPoolEntryHelper&
StorageHelperImpl::getExternalSystemAccountIDPoolEntryHelper()
{
    if (!mExternalSystemAccountIDPoolEntryHelper)
    {
        mExternalSystemAccountIDPoolEntryHelper =
            std::make_unique<ExternalSystemAccountIDPoolEntryHelperImpl>(*this);
    }
    return *mExternalSystemAccountIDPoolEntryHelper;
}
AccountRoleHelper&
StorageHelperImpl::getAccountRoleHelper()
{
    if (!mAccountRoleHelper)
    {
        mAccountRoleHelper = std::make_unique<AccountRoleHelper>(*this);
    }
    return *mAccountRoleHelper;
}
AccountRolePermissionHelperImpl&
StorageHelperImpl::getAccountRolePermissionHelper()
{
    if (!mAccountRolePermissionHelper)
    {
        mAccountRolePermissionHelper = std::make_unique<AccountRolePermissionHelperImpl>(*this);
    }
    return *mAccountRolePermissionHelper;
}

} // namespace stellar