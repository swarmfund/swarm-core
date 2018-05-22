
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

const std::regex IdentityPolicyFrame::resourceRegEx = std::regex(R"(^resource_type:[a-zA-Z]*:[a-zA-Z]*:[a-zA-Z0-9]*$)");

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
    if (IdentityPolicyFrame::isEffectValid(mIdentityPolicyEntry.effect))
    {
        throw std::runtime_error("Identity policy effect invalid");
    }
}

void
IdentityPolicyFrame::ensureValid() const
{
    ensureValid(mIdentityPolicyEntry);
}

bool IdentityPolicyFrame::isResourceValid(std::string const& resource)
{
    return std::regex_match(resource, IdentityPolicyFrame::resourceRegEx);
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
