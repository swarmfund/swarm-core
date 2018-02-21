#pragma once

#include "ledger/EntryFrame.h"

namespace soci
{
class session;
}

namespace stellar
{
class StatementContext;

class ExternalSystemAccountIDPoolEntryFrame : public EntryFrame
{
    ExternalSystemAccountIDPoolEntry& mExternalSystemAccountIDPoolEntry;

    ExternalSystemAccountIDPoolEntryFrame(ExternalSystemAccountIDPoolEntryFrame const& from);

public:
    typedef std::shared_ptr<ExternalSystemAccountIDPoolEntryFrame> pointer;

    ExternalSystemAccountIDPoolEntryFrame();

    explicit ExternalSystemAccountIDPoolEntryFrame(LedgerEntry const& from);

    ExternalSystemAccountIDPoolEntryFrame& operator=(
            ExternalSystemAccountIDPoolEntryFrame const& other);

    EntryFrame::pointer copy() const override
    {
        return EntryFrame::pointer(new ExternalSystemAccountIDPoolEntryFrame(*this));
    }

    static pointer createNew(uint64 poolEntryID, ExternalSystemType externalSystemType, std::string data);

    ExternalSystemAccountIDPoolEntry const& getExternalSystemAccountIDPoolEntry() const
    {
        return mExternalSystemAccountIDPoolEntry;
    }

    static void ensureValid(ExternalSystemAccountIDPoolEntry const& p);
    void ensureValid() const;
};
}
