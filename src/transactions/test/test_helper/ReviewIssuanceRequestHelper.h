#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "overlay/StellarXDR.h"
#include "ReviewRequestTestHelper.h"
#include "ledger/ReviewableRequestFrame.h"
#include "ledger/AssetFrame.h"
#include "ledger/BalanceFrame.h"

namespace stellar
{
namespace txtest 
{	
	class ReviewIssuanceChecker : public ReviewChecker
	{
	public:
		ReviewIssuanceChecker(const TestManager::pointer& testManager, const uint64_t requestID);
		ReviewIssuanceChecker(const TestManager::pointer& testManager,
							  std::shared_ptr<IssuanceRequest> issuanceRequest);

		void checkApprove(ReviewableRequestFrame::pointer request) override;

	protected:
		std::shared_ptr<IssuanceRequest> issuanceRequest;
		AssetFrame::pointer assetFrameBeforeTx;
		BalanceFrame::pointer balanceBeforeTx;
		BalanceFrame::pointer commissionBalanceBeforeTx;
	};
	class ReviewIssuanceRequestHelper : public ReviewRequestHelper
	{
	public:
		explicit ReviewIssuanceRequestHelper(TestManager::pointer testManager);

		ReviewRequestResult
		applyReviewRequestTx(Account & source, uint64_t requestID, Hash requestHash, ReviewableRequestType requestType,
							 ReviewRequestOpAction action, std::string rejectReason,
							 ReviewRequestResultCode expectedResult = ReviewRequestResultCode::SUCCESS) override;

        ReviewRequestResult
        applyReviewRequestTx(Account & source, uint64_t requestID, ReviewRequestOpAction action,
                             std::string rejectReason,
                             ReviewRequestResultCode expectedResult = ReviewRequestResultCode::SUCCESS);

        TransactionFramePtr
        createReviewRequestTx(Account& source, uint64_t requestID, Hash requestHash, ReviewableRequestType requestType,
                              ReviewRequestOpAction action, std::string rejectReason) override;

	};
}
}
