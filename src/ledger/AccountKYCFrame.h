//
// Created by volodymyr on 03.01.18.
//

#ifndef STELLAR_ACCOUNTKYCFRAME_H
#define STELLAR_ACCOUNTKYCFRAME_H


#include "EntryFrame.h"

namespace stellar
{

class AccountKYCFrame: public EntryFrame
{
private:
    AccountKYCEntry& mAccountKYCEntry;

    AccountKYCFrame(AccountKYCFrame const& from);

public:
    typedef std::shared_ptr<AccountKYCFrame> pointer;

    AccountKYCFrame();
    explicit AccountKYCFrame(LedgerEntry const& from);

    EntryFrame::pointer copy() const override
    {
        return EntryFrame::pointer(new AccountKYCFrame(*this));
    }

    AccountKYCEntry const& getAccountKYC() const
    {
        return mAccountKYCEntry;
    }

    AccountKYCEntry& getAccountKYC()
    {
        clearCached();
        return mAccountKYCEntry;
    }

    AccountID getID()
    {
        return mAccountKYCEntry.accountID;
    }

    std::string getKYCData()
    {
        return mAccountKYCEntry.KYCData;
    }

    void setKYCData(const std::string& KYCData)
    {
        mAccountKYCEntry.KYCData = longstring(KYCData);
    }

    static AccountKYCFrame::pointer createNew(AccountID accountID, std::string KYCData);

};

}




#endif //STELLAR_ACCOUNTKYCFRAME_H
