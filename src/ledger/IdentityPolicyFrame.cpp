
#include "IdentityPolicyFrame.h"
#include "LedgerDelta.h"
#include "database/Database.h"
#include "lib/util/format.h"
#include "util/basen.h"
#include "util/types.h"

#include <exception>

namespace stellar
{

using xdr::operator<;

IdentityPolicyFrame::IdentityPolicyFrame()
    : EntryFrame(LedgerEntryType::IDENTITY_POLICY)
    , mIdentityPolicyEntry(mEntry.data.identityPolicy())
{
}

IdentityPolicyFrame::IdentityPolicyFrame(LedgerEntry const& from)
    : EntryFrame(from), mIdentityPolicyEntry(mEntry.data.identityPolicy())
{
}

IdentityPolicyFrame::IdentityPolicyFrame(IdentityPolicyFrame const& from)
    : IdentityPolicyFrame(from.mEntry)
{
}

void
IdentityPolicyFrame::ensureValid(const IdentityPolicyEntry& mIdentityPolicyEntry)
{
    if (!IdentityPolicyFrame::isEffectValid(mIdentityPolicyEntry.effect))
    {
        throw std::runtime_error("Identity policy effect invalid");
    }
}

void
IdentityPolicyFrame::ensureValid() const
{
    ensureValid(mIdentityPolicyEntry);
}

bool IdentityPolicyFrame::isEffectValid(Effect const effect)
{
    return isValidEnumValue(effect);
}

EntryFrame::pointer
IdentityPolicyFrame::copy() const
{
    return EntryFrame::pointer(new IdentityPolicyFrame(*this));
}

} // namespace stellar
