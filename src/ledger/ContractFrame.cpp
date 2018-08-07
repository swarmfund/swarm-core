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
        mContract.details.emplace_back(details);
    }

    bool ContractFrame::addContractorConfirmation()
    {
        if ((mContract.statusInfo.status() == ContractState::BOTH_CONFIRMED) ||
            (mContract.statusInfo.status() == ContractState::CONTRACTOR_CONFIRMED))
            return false;

        if (mContract.statusInfo.status() == ContractState::CUSTOMER_CONFIRMED)
        {
            mContract.statusInfo.status(ContractState::BOTH_CONFIRMED);
            return true;
        }

        mContract.statusInfo.status(ContractState::CONTRACTOR_CONFIRMED);
        return true;
    }

    bool ContractFrame::addCustomerConfirmation()
    {
        if ((mContract.statusInfo.status() == ContractState::BOTH_CONFIRMED) ||
            (mContract.statusInfo.status() == ContractState::CUSTOMER_CONFIRMED))
            return false;

        if (mContract.statusInfo.status() == ContractState::CONTRACTOR_CONFIRMED)
        {
            mContract.statusInfo.status(ContractState::BOTH_CONFIRMED);
            return true;
        }

        mContract.statusInfo.status(ContractState::CUSTOMER_CONFIRMED);
        return true;
    }

    void
    ContractFrame::setDisputer(AccountID const& disputer)
    {
        if (mContract.statusInfo.status() != ContractState::DISPUTING)
            setStatus(ContractState::DISPUTING);

        mContract.statusInfo.disputeDetails().disputer = disputer;
    }

    void
    ContractFrame::setDisputeReason(longstring const& reason)
    {
        if (mContract.statusInfo.status() != ContractState::DISPUTING)
            setStatus(ContractState::DISPUTING);

        mContract.statusInfo.disputeDetails().reason = reason;
    }
}
