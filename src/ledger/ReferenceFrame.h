#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryFrame.h"
#include <functional>
#include <unordered_map>

namespace soci
{
class session;
}

namespace stellar
{
class StatementContext;

class ReferenceFrame : public EntryFrame
{

    ReferenceEntry& mReference;

    ReferenceFrame(ReferenceFrame const& from);

  public:
    typedef std::shared_ptr<ReferenceFrame> pointer;

    ReferenceFrame();
    ReferenceFrame(LedgerEntry const& from);

    ReferenceFrame& operator=(ReferenceFrame const& other);

	static ReferenceFrame::pointer create(AccountID sender, stellar::string64 reference);

    EntryFrame::pointer
    copy() const override
    {
        return EntryFrame::pointer(new ReferenceFrame(*this));
    }

    ReferenceEntry const&
    getReference() const
    {
        return mReference;
    }
    ReferenceEntry&
    getReference()
    {
        return mReference;
    }

    AccountID getSender(){
        return mReference.sender;
    }

    std::string getReferenceString(){
        return mReference.reference;
    }

    static bool isValid(ReferenceEntry const& oe);
    bool isValid() const;
};
}
