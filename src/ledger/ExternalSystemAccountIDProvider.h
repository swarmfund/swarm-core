#pragma once

#include "ledger/EntryFrame.h"

namespace soci
{
class session;
}

namespace stellar
{
class StatementContext;

class ExternalSystemAccountIDProviderFrame : public EntryFrame
{
    ExternalSystemAccountIDProvider& mExternalSystemAccountIDProvider;

    ExternalSystemAccountIDProviderFrame(ExternalSystemAccountIDProviderFrame const& from);

public:
    typedef std::shared_ptr<ExternalSystemAccountIDProviderFrame> pointer;

    ExternalSystemAccountIDProviderFrame();

    explicit ExternalSystemAccountIDProviderFrame(LedgerEntry const& from);

    ExternalSystemAccountIDProviderFrame& operator=(
            ExternalSystemAccountIDProviderFrame const& other);

    EntryFrame::pointer copy() const override
    {
        return EntryFrame::pointer(new ExternalSystemAccountIDProviderFrame(*this));
    }

    static pointer createNew(uint64 providerID, ExternalSystemType externalSystemType, std::string data);

    ExternalSystemAccountIDProvider const& getExternalSystemAccountIDProvider() const
    {
        return mExternalSystemAccountIDProvider;
    }

    static bool isValid(ExternalSystemAccountIDProvider const& p);
    bool isValid() const;
};
}
