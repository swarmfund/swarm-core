
#include "IdentityPolicyFrame.h"
#include "LedgerDelta.h"
#include "database/Database.h"
#include "lib/util/format.h"
#include "util/basen.h"
#include "util/types.h"

#include <exception>
#include <regex>

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
IdentityPolicyFrame::ensureValid(
    const IdentityPolicyEntry& mIdentityPolicyEntry)
{
    if (IdentityPolicyFrame::isResourceValid(mIdentityPolicyEntry.resource))
    {
        throw std::runtime_error("Identity policy resource invalid");
    }
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
    const std::regex resourceRegEx{R"(^resource_type:.+:.+:.+$)"};

    return std::regex_match(resource, resourceRegEx);
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
