#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryFrame.h"

namespace soci
{
class session;
}

namespace stellar
{
class StatementContext;

class ExternalSystemAccountIDFrame : public EntryFrame
{

    ExternalSystemAccountID& mExternalSystemAccountID;

    ExternalSystemAccountIDFrame(ExternalSystemAccountIDFrame const& from);

public:
    typedef std::shared_ptr<ExternalSystemAccountIDFrame> pointer;

    ExternalSystemAccountIDFrame();
    ExternalSystemAccountIDFrame(LedgerEntry const& from);

    ExternalSystemAccountIDFrame& operator=(
        ExternalSystemAccountIDFrame const& other);

    EntryFrame::pointer copy() const override
    {
        return EntryFrame::pointer(new ExternalSystemAccountIDFrame(*this));
    }

    static pointer createNew(AccountID const accountID, ExternalSystemType const externalSystemType, std::string const data);

    ExternalSystemAccountID const& getExternalSystemAccountID() const
    {
        return mExternalSystemAccountID;
    }

    static bool isValid(ExternalSystemAccountID const& oe);
    bool isValid() const;

};
}
