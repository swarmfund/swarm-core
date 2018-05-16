
#include "EntityTypeFrame.h"
#include "LedgerDelta.h"
#include "database/Database.h"
#include "lib/util/format.h"
#include "util/basen.h"
#include "util/types.h"

#include <exception>

namespace stellar
{

using xdr::operator<;

EntityTypeFrame::EntityTypeFrame()
    : EntryFrame(LedgerEntryType::ENTITY_TYPE)
    , mTypeEntry(mEntry.data.entityType())
{
}

EntityTypeFrame::EntityTypeFrame(LedgerEntry const& from)
    : EntryFrame(from), mTypeEntry(mEntry.data.entityType())
{
}

EntityTypeFrame::EntityTypeFrame(EntityTypeFrame const& from)
    : EntityTypeFrame(from.mEntry)
{
}

void
EntityTypeFrame::ensureValid(const EntityTypeEntry& mTypeEntry)
{
    if (EntityTypeFrame::isNameValid(mTypeEntry.name))
    {
        throw std::runtime_error("Entity types name invalid");
    }
    if (EntityTypeFrame::isTypeValid(mTypeEntry.type))
    {
        throw std::runtime_error("Entity types type invalid");
    }
}

void
EntityTypeFrame::ensureValid() const
{
    ensureValid(mTypeEntry);
}

bool
EntityTypeFrame::isNameValid(const std::string& name)
{
    return !name.empty();
}

bool
EntityTypeFrame::isTypeValid(const EntityType& name)
{
    return isValidEnumValue(name);
}

EntryFrame::pointer
EntityTypeFrame::copy() const
{
    return EntryFrame::pointer(new EntityTypeFrame(*this));
}

} // namespace stellar
