#pragma once
#include "transactions/OperationFrame.h"
#include "ledger/ReviewableRequestFrame.h"
#include "ledger/ReviewableRequestHelper.h"

namespace stellar
{
	class CreateKYCRequestOpFrame : public OperationFrame {
		CreateKYCRequestOp mCreateKYCRequest;
		std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(Database& db, LedgerDelta* delta) const override;
		SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const override;
		CreateKYCRequestResult&
			innerResult()
		{
			return mResult.tr().createKYCRequestResult();
		}
		std::string getReference() const;
		void buildRequest(ReviewableRequestEntry& requestEntry);
		bool updateKYCRequest(ReviewableRequestHelper* requestHelper, Database& db, LedgerDelta& delta, Application& app);
	public:
		CreateKYCRequestOpFrame(Operation const& op, OperationResult& res,
			TransactionFrame& parentTx);

		bool doApply(Application& app, LedgerDelta& delta,
			LedgerManager& ledgerManager) override;
		bool doCheckValid(Application& app) override;
		static CreateKYCRequestResultCode
			getInnerCode(OperationResult const& res)
		{
			return res.tr().createKYCRequestResult().code();
		}
		std::string getInnerResultCodeAsStr() override {
			return xdr::xdr_traits<CreateKYCRequestResultCode>::enum_name(innerResult().code());
		}
		
	};
}