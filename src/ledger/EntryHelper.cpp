#include <xdrpp/marshal.h>
#include "ledger/EntryHelper.h"

namespace stellar
{

void
EntryHelper::flushCachedEntry(LedgerKey const& key)
{
    auto s = binToHex(xdr::xdr_to_opaque(key));
    getDatabase().getEntryCache().erase_if_exists(s);
}

bool
EntryHelper::cachedEntryExists(LedgerKey const& key)
{
    auto s = binToHex(xdr::xdr_to_opaque(key));
    return getDatabase().getEntryCache().exists(s);
}

std::shared_ptr<LedgerEntry const>
EntryHelper::getCachedEntry(LedgerKey const& key)
{
    auto s = binToHex(xdr::xdr_to_opaque(key));
    return getDatabase().getEntryCache().get(s);
}

void
EntryHelper::putCachedEntry(LedgerKey const& key,
                            std::shared_ptr<LedgerEntry const> p)
{
    auto s = binToHex(xdr::xdr_to_opaque(key));
    getDatabase().getEntryCache().put(s, p);
}

} // namespace stellar