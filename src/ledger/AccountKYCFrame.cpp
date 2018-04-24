//
// Created by volodymyr on 03.01.18.
//

#include "AccountKYCFrame.h"

namespace stellar {

    AccountKYCFrame::AccountKYCFrame()
            : EntryFrame(LedgerEntryType::ACCOUNT_KYC), mAccountKYCEntry(mEntry.data.accountKYC())
    {
    }

    AccountKYCFrame::AccountKYCFrame(LedgerEntry const& from)
            : EntryFrame(from), mAccountKYCEntry(mEntry.data.accountKYC())
    {
    }

    AccountKYCFrame::AccountKYCFrame(AccountKYCFrame const& from)
            : AccountKYCFrame(from.mEntry)
    {
    }

    AccountKYCFrame::pointer AccountKYCFrame::createNew(AccountID accountID, std::string KYCData)
    {
        LedgerEntry ledgerEntry;
        ledgerEntry.data.type(LedgerEntryType::ACCOUNT_KYC);
        auto& accountKYC = ledgerEntry.data.accountKYC();
        accountKYC.accountID = accountID;
        accountKYC.KYCData = KYCData;

        return std::make_shared<AccountKYCFrame>(ledgerEntry);
    }

}


