#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "overlay/StellarXDR.h"
#include "transactions/test_helper/TxHelper.h"
#include "ledger/ReviewableRequestFrame.h"

namespace stellar
{
namespace txtest 
{	
	class ReviewRequestHelper : public TxHelper
	{

	protected:
		virtual void checkApproval(ReviewableRequestFrame::pointer requestBeforeTx) = 0;
		ReviewRequestHelper(TestManager::pointer testManager);
	public:

		ReviewRequestResult applyReviewRequestTx(Account & source, uint64_t requestID, Hash requestHash, ReviewableRequestType requestType,
			ReviewRequestOpAction action, std::string rejectReason, 
			ReviewRequestResultCode expectedResult = ReviewRequestResultCode::REVIEW_REQUEST_SUCCESS);
		TransactionFramePtr createReviewRequestTx(Account& source, uint64_t requestID, Hash requestHash, ReviewableRequestType requestType, 
			ReviewRequestOpAction action, std::string rejectReason);
	};
}
}
