#pragma once

#include <xdr/Stellar-ledger-entries-contract.h>
#include "ledger/EntryFrame.h"

namespace  soci
{
    class session;
}

namespace stellar
{
class StatementContext;

class ContractFrame : public EntryFrame
{
    ContractEntry& mContract;

    ContractFrame(ContractFrame const& from);

public:
    typedef std::shared_ptr<ContractFrame> pointer;

    ContractFrame();
    ContractFrame(LedgerEntry const& from);

    ContractFrame& operator=(ContractFrame const& other);

    EntryFrame::pointer
    copy() const override
    {
        return EntryFrame::pointer(new ContractFrame(*this));
    }

    ContractEntry const&
    getContract() const
    {
        return mContract;
    }
};

}




