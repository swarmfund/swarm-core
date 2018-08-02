#include "ledger/KeyValueHelper.h"
#include "ledger/StorageHelperImpl.h"
#include "ledger/ExternalSystemAccountIDHelper.h"
#include "ledger/ExternalSystemAccountIDPoolEntryHelper.h"
#include "ledger/LedgerDelta.h"

namespace stellar
{

StorageHelperImpl::StorageHelperImpl(Database& db, LedgerDelta& ledgerDelta)
    : mDatabase(db), mLedgerDelta(ledgerDelta)
{
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
LedgerDelta&
StorageHelperImpl::getLedgerDelta()
{
    return mLedgerDelta;
}
const LedgerDelta&
StorageHelperImpl::getLedgerDelta() const
{
    return mLedgerDelta;
}

void
StorageHelperImpl::commit()
{
    mLedgerDelta.commit();
}
void
StorageHelperImpl::rollback()
{
    mLedgerDelta.rollback();
}

KeyValueHelper&
StorageHelperImpl::getKeyValueHelper()
{
    if (!mKeyValueHelper)
    {
        mKeyValueHelper = std::make_unique<KeyValueHelper>();
	}
    return *mKeyValueHelper;
}
ExternalSystemAccountIDHelper&
StorageHelperImpl::getExternalSystemAccountIDHelper()
{
    if (!mExternalSystemAccountIDHelper)
    {
        mExternalSystemAccountIDHelper =
            std::make_unique<ExternalSystemAccountIDHelper>();
    }
    return *mExternalSystemAccountIDHelper;
}
ExternalSystemAccountIDPoolEntryHelper&
StorageHelperImpl::getExternalSystemAccountIDPoolEntryHelper()
{
	if (!mExternalSystemAccountIDPoolEntryHelper)
	{
            mExternalSystemAccountIDPoolEntryHelper =
                std::make_unique<ExternalSystemAccountIDPoolEntryHelper>();
	}
    return *mExternalSystemAccountIDPoolEntryHelper;
}

} // namespace stellar