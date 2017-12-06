// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ReviewRequestHelper.h"
#include "ledger/ReviewableRequestFrame.h"
#include "ledger/ReviewableRequestHelper.h"
#include "transactions/review_request/ReviewRequestOpFrame.h"
#include <functional>



namespace stellar
{

namespace txtest
{
	ReviewRequestHelper::ReviewRequestHelper(TestManager::pointer testManager): TxHelper(testManager)
	{
	}
	ReviewRequestResult ReviewRequestHelper::applyReviewRequestTx(Account & source, uint64_t requestID, Hash requestHash,
		ReviewableRequestType requestType, ReviewRequestOpAction action, std::string rejectReason,
		ReviewRequestResultCode expectedResult, std::function<void(ReviewableRequestFrame::pointer requestBeforeTx)> checkApproval)
	{
		auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
		auto reviewableRequestCountBeforeTx = reviewableRequestHelper->countObjects(mTestManager->getDB().getSession());
		LedgerDelta& delta = mTestManager->getLedgerDelta();
		auto requestBeforeTx = reviewableRequestHelper->loadRequest(requestID, mTestManager->getDB(), &delta);
		auto txFrame = createReviewRequestTx(source, requestID, requestHash, requestType, action, rejectReason);

		mTestManager->applyCheck(txFrame);
		auto txResult = txFrame->getResult();
		auto opResult = txResult.result.results()[0];
		auto actualResultCode = ReviewRequestOpFrame::getInnerCode(opResult);
		REQUIRE(actualResultCode == expectedResult);

		auto reviewResult = opResult.tr().reviewRequestResult();
		if (expectedResult != ReviewRequestResultCode::SUCCESS)
		{
			uint64 reviewableRequestCountAfterTx = reviewableRequestHelper->countObjects(mTestManager->getDB().getSession());
			REQUIRE(reviewableRequestCountBeforeTx == reviewableRequestCountAfterTx);
			return reviewResult;
		}

		REQUIRE(!!requestBeforeTx);

		auto requestAfterTx = reviewableRequestHelper->loadRequest(requestID, mTestManager->getDB(), &delta);
		if (action == ReviewRequestOpAction::REJECT) {
			REQUIRE(!!requestAfterTx);
			REQUIRE(requestAfterTx->getRejectReason() == rejectReason);
			return reviewResult;
		}

		// approval and permanent reject must delete request
		REQUIRE(!requestAfterTx);
		if (action == ReviewRequestOpAction::PERMANENT_REJECT) {
			return reviewResult;
		}

		checkApproval(requestBeforeTx);
		return reviewResult;
	}

	TransactionFramePtr ReviewRequestHelper::createReviewRequestTx(Account & source, uint64_t requestID, Hash requestHash, ReviewableRequestType requestType, ReviewRequestOpAction action, std::string rejectReason)
	{
		Operation op;
		op.body.type(OperationType::REVIEW_REQUEST);
		ReviewRequestOp& reviewRequestOp = op.body.reviewRequestOp();
		reviewRequestOp.action = action;
		reviewRequestOp.reason = rejectReason;
		reviewRequestOp.requestHash = requestHash;
		reviewRequestOp.requestID = requestID;
		reviewRequestOp.requestType = requestType;
		return txFromOperation(source, op, nullptr);
	}
}

}
