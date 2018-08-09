#include "ContractFrame.h"

namespace stellar
{

    ContractFrame::ContractFrame() : EntryFrame(LedgerEntryType::CONTRACT), mContract(mEntry.data.contract())
    {
    }

    ContractFrame::ContractFrame(LedgerEntry const& from) : EntryFrame(from), mContract(mEntry.data.contract())
    {
    }

    ContractFrame::ContractFrame(ContractFrame const& from) : ContractFrame(from.mEntry)
    {
    }

    ContractFrame& ContractFrame::operator=(ContractFrame const& other)
    {
        if (&other != this)
        {
            mContract = other.mContract;
            mKey = other.mKey;
            mKeyCalculated = other.mKeyCalculated;
        }
        return *this;
    }

    void
    ContractFrame::addContractDetails(longstring const& details)
    {
        mContract.details.emplace_back(details);
    }

    void
    ContractFrame::addInvoice(uint64_t const& requestID)
    {
        mContract.invoiceRequestsIDs.emplace_back(requestID);
    }

    bool ContractFrame::addState(ContractState state)
    {
        if (!(static_cast<int32_t>(mContract.stateInfo.state()) &
              static_cast<int32_t>(state)))
            return false;

        mContract.stateInfo.state(
                static_cast<ContractState>(
                static_cast<int32_t>(mContract.stateInfo.state()) |
                static_cast<int32_t>(state)));
        return true;
    }

    void ContractFrame::startDispute(AccountID const& disputer,
                                     longstring const& reason)
    {
        if (!(static_cast<int32_t>(mContract.stateInfo.state()) &
              static_cast<int32_t>(ContractState::DISPUTING)))
            addState(ContractState::DISPUTING);

        mContract.stateInfo.disputeDetails().disputer = disputer;
        mContract.stateInfo.disputeDetails().reason = reason;
    }
}
