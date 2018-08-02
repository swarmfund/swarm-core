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

    void ContractFrame::addContractDetails(longstring const& details)
    {
        mContract.details += details;
    }

    void ContractFrame::addInvoiceRequest(uint64_t const& requestID)
    {
        mContract.invoiceRequestIDs.emplace_back(requestID);
    }

    /*ContractFrame::pointer
    ContractFrame::createNew()
    {
        LedgerEntry le;
        le.data.type(LedgerEntryType::CONTRACT);
        ContractEntry& entry = le.data.contract();

        return std::make_shared<ContractFrame>(le);
    }*/
}
