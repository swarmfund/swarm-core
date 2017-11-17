#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/OperationFrame.h"
#include "ledger/CoinsEmissionRequestFrame.h"

namespace stellar
{
class ManageCoinsEmissionRequestOpFrame : public OperationFrame
{
    ManageCoinsEmissionRequestResult&
    innerResult()
    {
        return mResult.tr().manageCoinsEmissionRequestResult();
    }

    ManageCoinsEmissionRequestOp const& mManageCoinsEmissionRequest;

	std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(Database& db, LedgerDelta* delta) const override;
	SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const override;

  public:
    ManageCoinsEmissionRequestOpFrame(Operation const& op, OperationResult& res,
                       TransactionFrame& parentTx);

    bool doApply(Application& app, LedgerDelta& delta,
                 LedgerManager& ledgerManager) override;
    bool doCheckValid(Application& app) override;

    static ManageCoinsEmissionRequestResultCode
    getInnerCode(OperationResult const& res)
    {
        return res.tr().manageCoinsEmissionRequestResult().code();
    }


	static bool emitTokens(Application& app, Database& db, LedgerDelta& delta, LedgerManager& ledgerManager, AccountFrame::pointer destAccount, AccountID& issuer, int64_t  amount, AssetCode& token, EmissionFeeType emissionFeeType);

	static CoinsEmissionRequestFrame::pointer tryCreateEmissionRequest(Application& app, Database& db, LedgerDelta& delta, LedgerManager& ledgerManager, int64_t amount, AssetCode asset, std::string reference, AccountID issuer,
		BalanceFrame::pointer receiver);
};
}
