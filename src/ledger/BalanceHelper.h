#pragma once

#include "BalanceFrame.h"
#include "EntryHelper.h"
#include <functional>

namespace soci
{
class session;
}

namespace stellar
{
class StatementContext;

class BalanceHelper : public EntryHelper
{
  public:
    virtual BalanceFrame::pointer loadBalance(BalanceID balanceID) = 0;

    virtual BalanceFrame::pointer loadBalance(BalanceID balanceID,
                                              AccountID accountID) = 0;

    virtual BalanceFrame::pointer loadBalance(AccountID accountID,
                                              AssetCode assetCode) = 0;

    virtual std::vector<BalanceFrame::pointer>
    loadBalances(AccountID accountID, AssetCode assetCode) = 0;

    virtual std::vector<BalanceFrame::pointer>
    loadBalances(std::vector<AccountID> accountIDs, AssetCode assetCode) = 0;

    virtual std::vector<BalanceFrame::pointer>
    loadAssetHolders(AssetCode assetCode, AccountID owner,
                     uint64_t minTotalAmount) = 0;

  private:
    virtual void
    loadBalances(StatementContext& prep,
                 std::function<void(LedgerEntry const&)> balanceProcessor) = 0;
};
}