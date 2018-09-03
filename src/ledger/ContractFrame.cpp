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
    ContractFrame::addInvoice(uint64_t const& requestID)
    {
        mContract.invoiceRequestsIDs.emplace_back(requestID);
    }

    bool ContractFrame::addState(ContractState state)
    {
        if (mContract.state & static_cast<uint32_t>(state))
            return false;

        mContract.state |= static_cast<uint32_t>(state);
        return true;
    }

    bool ContractFrame::isBothConfirmed()
    {
        return (mContract.state &
                static_cast<int32_t>(ContractState::CUSTOMER_CONFIRMED)) &&
               (mContract.state &
                static_cast<int32_t>(ContractState::CONTRACTOR_CONFIRMED));
    }

    longstring ContractFrame::getCustomerDetails()
    {
        switch (mContract.ext.v())
        {
            case LedgerVersion::ADD_CUSTOMER_DETAILS_TO_CONTRACT:
                return mContract.ext.customerDetails();
            case LedgerVersion::EMPTY_VERSION:
                return "";
            default:
                throw std::runtime_error("Unexpected case in contract ext.");
        }
    }

    void ContractFrame::setCustomerDetails(ContractEntry& contract, longstring customerDetails)
    {
        switch (contract.ext.v()) {
            case LedgerVersion::ADD_CUSTOMER_DETAILS_TO_CONTRACT:
                contract.ext.customerDetails() = customerDetails;
                return;
            default:
                if (customerDetails.empty()) {
                    return;
                }
                throw std::runtime_error("Unexpected action: not able to set customer details for contract of unexpected version");
        }
    }
}
