//
// Created by volodymyr on 03.01.18.
//

#ifndef STELLAR_ACCOUNTKYCHELPER_H
#define STELLAR_ACCOUNTKYCHELPER_H

#include "EntryHelperLegacy.h"
#include "AccountKYCFrame.h"
#include "LedgerDelta.h"

namespace stellar
{

class AccountKYCHelper : public EntryHelperLegacy
{
private:
    AccountKYCHelper() { ; }
    ~AccountKYCHelper() { ; }

    void storeUpdate(LedgerDelta& delta, Database& db, const LedgerEntry& entry, bool insert);
public:
    AccountKYCHelper(AccountKYCHelper const&) = delete;
    AccountKYCHelper& operator=(AccountKYCHelper const&) = delete;

    static AccountKYCHelper* Instance()
    {
        static AccountKYCHelper singleton;
        return &singleton;
    }

    EntryFrame::pointer fromXDR(LedgerEntry const &from) override;

    void dropAll(Database& db) override;

    EntryFrame::pointer storeLoad(LedgerKey const &ledgerKey, Database &db) override;
    void storeAdd(LedgerDelta& delta, Database& db, LedgerEntry const& entry) override;
    void storeChange(LedgerDelta& delta, Database& db, LedgerEntry const& entry) override;
    void storeDelete(LedgerDelta &delta, Database &db, LedgerKey const &key) override;

    bool exists(Database &db, LedgerKey const &key) override;
    uint64_t countObjects(soci::session &sess) override;

    LedgerKey getLedgerKey(LedgerEntry const& from) override;

    AccountKYCFrame::pointer loadAccountKYC(const AccountID &accountID, Database &db, LedgerDelta *delta = nullptr);
};

}


#endif //STELLAR_ACCOUNTKYCHELPER_H