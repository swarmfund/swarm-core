
#pragma once

#include "ledger/AccountFrame.h"
#include "ledger/EntryFrame.h"
#include "util/types.h"
#include <functional>
#include <stdio.h>
#include <unordered_map>

namespace stellar
{
class EntityTypeFrame : public EntryFrame
{

    EntityTypeEntry& mTypeEntry;
    EntityTypeFrame(EntityTypeFrame const& from);

  public:
    EntityTypeFrame();
    explicit EntityTypeFrame(LedgerEntry const& from);

    using pointer = std::shared_ptr<EntityTypeFrame>;

    EntityTypeEntry&
    getEntityType() const
    {
        return mTypeEntry;
    }

    int64_t
    getEntityTypeID() const
    {
        return mTypeEntry.id;
    }

    std::string
    getEntityTypeName() const
    {
        return mTypeEntry.name;
    }

    EntityType
    getEntityTypeValue() const
    {

        return mTypeEntry.type;
    }

    EntryFrame::pointer copy() const override;

    static bool isNameValid(std::string const& name);
    static bool isTypeValid(EntityType const& name);

    static void ensureValid(EntityTypeEntry const& oeEntry);
    void ensureValid() const;
};

} // namespace stellar
