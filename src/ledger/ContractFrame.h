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

    ContractEntry&
    getContract()
    {
        return mContract;
    }

    uint64_t const&
    getContractID() const
    {
        return mContract.contractID;
    }

    AccountID const&
    getContractor() const
    {
        return mContract.contractor;
    }

    AccountID const&
    getCustomer() const
    {
        return mContract.customer;
    }

    AccountID const&
    getEscrow() const
    {
        return mContract.escrow;
    }

    uint64_t const&
    getStartTime() const
    {
        return mContract.startTime;
    }

    uint64_t const&
    getEndTime() const
    {
        return mContract.endTime;
    }

    ContractStatus const
    getStatus() const
    {
        return mContract.statusInfo.status();
    }

    uint64_t const
    getContractDetailsCount()
    {
        return mContract.details.size();
    }

    void
    setStatus(ContractStatus status)
    {
        mContract.statusInfo.status(status);
    }

    void setDisputer(AccountID const& disputer);

    void addContractDetails(longstring const& details);

    bool addContractorConfirmation();
    bool addCustomerConfirmation();
};

}




