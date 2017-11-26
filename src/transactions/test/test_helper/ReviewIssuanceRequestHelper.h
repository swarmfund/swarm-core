#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "overlay/StellarXDR.h"
#include "ReviewRequestHelper.h"
#include "ledger/ReviewableRequestFrame.h"
#include "ledger/AssetFrame.h"
#include "ledger/BalanceFrame.h"

namespace stellar
{
namespace txtest 
{	
	class ReviewIssuanceRequestHelper : public ReviewRequestHelper
	{
	private:
		std::shared_ptr<IssuanceRequest> tryLoadIssuanceRequest(uint64_t requestID);

	protected:

		AssetFrame::pointer tryLoadAssetFrameForRequest(uint64_t requestID);
		BalanceFrame::pointer tryLoadBalanceForRequest(uint64_t requestID);
	public:
		ReviewIssuanceRequestHelper(TestManager::pointer testManager);

		ReviewRequestResult applyReviewRequestTx(Account & source, uint64_t requestID, Hash requestHash, ReviewableRequestType requestType,
			ReviewRequestOpAction action, std::string rejectReason,
			ReviewRequestResultCode expectedResult = ReviewRequestResultCode::REVIEW_REQUEST_SUCCESS) override;

		ReviewRequestResult applyReviewRequestTx(Account & source, uint64_t requestID, ReviewRequestOpAction action, std::string rejectReason,
			ReviewRequestResultCode expectedResult = ReviewRequestResultCode::REVIEW_REQUEST_SUCCESS);

		void checkApproval(IssuanceRequest issuanceRequest, AssetFrame::pointer assetFrameBeforeTx, BalanceFrame::pointer balanceBeforeTx);
	};
}
}
