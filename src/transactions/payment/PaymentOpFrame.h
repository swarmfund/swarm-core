#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/OperationFrame.h"
#include "ledger/BalanceHelperLegacy.h"

namespace stellar
{

class PaymentOpFrame : public OperationFrame
{
    PaymentResult&
    innerResult()
    {
        return mResult.tr().paymentResult();
    }
    PaymentOp const& mPayment;
    int64_t sourceSent;
    int64_t destReceived;
    
	std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(Database& db, LedgerDelta* delta) const override;
	SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                              int32_t ledgerVersion) const override;

	bool isRecipeintFeeNotRequired(Database& db);

	bool isAllowedToTransfer(Database& db, AssetFrame::pointer asset);
    bool processFees(Application& app, LedgerManager& lm, LedgerDelta& delta, Database& db);
    bool processFees_v1(Application& app, LedgerDelta& delta, Database& db);
    bool processFees_v2(Application& app, LedgerDelta& delta, Database& db);

protected:

      AccountFrame::pointer mDestAccount;
	  BalanceFrame::pointer mSourceBalance;
	  BalanceFrame::pointer mDestBalance;

	  // returns false, if failed to load (tx result is set on error)
	  bool tryLoadBalances(Application& app, Database& db, LedgerDelta& delta);
	  // returns false, if fees are insufficient
	  bool checkFeesV1(Application& app, Database& db, LedgerDelta& delta);
	  bool checkFees(Application& app, Database& db, LedgerDelta& delta);
	  // calculates amount source sent and destination received. Returns false on overflow
	  bool calculateSourceDestAmount();
  public:
    
    PaymentOpFrame(Operation const& op, OperationResult& res,
                   TransactionFrame& parentTx);

    bool doApply(Application& app, StorageHelper& storageHelper,
                 LedgerManager& ledgerManager) override;
    bool doCheckValid(Application& app) override;

    bool
    processInvoice(Application& app, LedgerDelta& delta, Database& db);

    static bool isTransferFeeMatch(AccountFrame::pointer account, AssetCode const& assetCode, FeeData const& feeData, int64_t const& amount, Database& db, LedgerDelta& delta);


    bool
    processBalanceChange(Application& app, AccountManager::Result balanceChangeResult);

    static PaymentResultCode
    getInnerCode(OperationResult const& res)
    {
        return res.tr().paymentResult().code();
    }
};
}
