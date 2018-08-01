#include "ledger/KeyValueHelper.h"
#include "ledger/StorageHelperImpl.h"
#include "ledger/ExternalSystemAccountIDHelper.h"
#include "ledger/ExternalSystemAccountIDPoolEntryHelper.h"

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
}
void
StorageHelperImpl::rollback()
{
}

KeyValueHelper&
StorageHelperImpl::getKeyValueHelper()
{
    return *KeyValueHelper::Instance(); // TMP
}
ExternalSystemAccountIDHelper&
StorageHelperImpl::getExternalSystemAccountIDHelper()
{
    return *ExternalSystemAccountIDHelper::Instance(); // TMP
}
ExternalSystemAccountIDPoolEntryHelper&
StorageHelperImpl::getExternalSystemAccountIDPoolEntryHelper()
{
    return *ExternalSystemAccountIDPoolEntryHelper::Instance(); // TMP
}

} // namespace stellar