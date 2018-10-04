// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <transactions/test/TxTests.h>
#include "IssuanceRequestHelper.h"
#include "ledger/AssetHelperLegacy.h"
#include "ledger/BalanceHelperLegacy.h"
#include "ledger/ReviewableRequestFrame.h"
#include "ledger/ReviewableRequestHelper.h"
#include "ledger/ReferenceFrame.h"
#include "ledger/ReferenceHelper.h"
#include "transactions/issuance/CreatePreIssuanceRequestOpFrame.h"
#include "transactions/issuance/CreateIssuanceRequestOpFrame.h"
#include "ReviewPreIssuanceRequestHelper.h"
#include "ReviewIssuanceRequestHelper.h"
#include "ManageAssetTestHelper.h"
#include "test/test_marshaler.h"


namespace stellar
{

namespace txtest
{
	IssuanceRequestHelper::IssuanceRequestHelper(TestManager::pointer testManager) : TxHelper(testManager)
	{
	}
	CreatePreIssuanceRequestResult IssuanceRequestHelper::applyCreatePreIssuanceRequest(Account & source,
                                                          SecretKey & preIssuedAssetSigner, AssetCode assetCode,
                                                          uint64_t amount, std::string reference,
                                                          CreatePreIssuanceRequestResultCode expectedResult)
	{
		auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
		auto reviewableRequestCountBeforeTx = reviewableRequestHelper->countObjects(mTestManager->getDB().getSession());

		auto referenceHelper = ReferenceHelper::Instance();
		auto referenceBeforeTx = referenceHelper->loadReference(source.key.getPublicKey(), reference, mTestManager->getDB());

        auto preIssuanceRequest = createPreIssuanceRequest(preIssuedAssetSigner, assetCode, amount, reference);
		auto txFrame = createPreIssuanceRequestTx(source, preIssuanceRequest);

        auto checker = ReviewPreIssuanceChecker(mTestManager, std::make_shared<PreIssuanceRequest>(preIssuanceRequest));

		mTestManager->applyCheck(txFrame);
		auto txResult = txFrame->getResult();
		auto opResult = txResult.result.results()[0];
		auto actualResultCode = CreatePreIssuanceRequestOpFrame::getInnerCode(opResult);
		REQUIRE(actualResultCode == expectedResult);

		uint64 reviewableRequestCountAfterTx = reviewableRequestHelper->countObjects(mTestManager->getDB().getSession());
		if (expectedResult != CreatePreIssuanceRequestResultCode::SUCCESS)
		{
			REQUIRE(reviewableRequestCountBeforeTx == reviewableRequestCountAfterTx);
			return CreatePreIssuanceRequestResult{};
		}

        auto createPreIssuanceResult = opResult.tr().createPreIssuanceRequestResult();
        if (source.key == getRoot())
        {
            REQUIRE(createPreIssuanceResult.success().fulfilled);
            checker.checkApprove(nullptr);
            return createPreIssuanceResult;
        }

		REQUIRE(!referenceBeforeTx);
		REQUIRE(reviewableRequestCountBeforeTx + 1 == reviewableRequestCountAfterTx);
        REQUIRE(!createPreIssuanceResult.success().fulfilled);
		return createPreIssuanceResult;
	}

	TransactionFramePtr IssuanceRequestHelper::createPreIssuanceRequestTx(Account &source, const PreIssuanceRequest &request)
	{
		Operation op;
		op.body.type(OperationType::CREATE_PREISSUANCE_REQUEST);
		CreatePreIssuanceRequestOp& createPreIssuanceRequestOp = op.body.createPreIssuanceRequest();
		createPreIssuanceRequestOp.request = request;
		createPreIssuanceRequestOp.ext.v(LedgerVersion::EMPTY_VERSION);
		return txFromOperation(source, op, nullptr);
	}

    PreIssuanceRequest IssuanceRequestHelper::createPreIssuanceRequest(SecretKey &preIssuedAssetSigner, AssetCode assetCode,
                                                                       uint64_t amount, std::string reference)
    {
        PreIssuanceRequest preIssuanceRequest;
        preIssuanceRequest.amount = amount;
        preIssuanceRequest.asset = assetCode;
        preIssuanceRequest.reference = reference;
        preIssuanceRequest.signature = createPreIssuanceRequestSignature(preIssuedAssetSigner, assetCode, amount, reference);
        preIssuanceRequest.ext.v(LedgerVersion::EMPTY_VERSION);
        return preIssuanceRequest;
    }

	DecoratedSignature IssuanceRequestHelper::createPreIssuanceRequestSignature(SecretKey & preIssuedAssetSigner, AssetCode assetCode, uint64_t amount, std::string reference)
	{
		auto signatureData = CreatePreIssuanceRequestOpFrame::getSignatureData(reference, amount, assetCode);
		DecoratedSignature sig;
		sig.signature = preIssuedAssetSigner.sign(signatureData);
		sig.hint = PubKeyUtils::getHint(preIssuedAssetSigner.getPublicKey());
		return sig;
	}

	CreateIssuanceRequestResult IssuanceRequestHelper::applyCreateIssuanceRequest(Account & source, AssetCode assetCode,
                                                                                  uint64_t amount, BalanceID receiver,
                                                                                  std::string reference,
																				  uint32_t *allTasks,
                                                                                  CreateIssuanceRequestResultCode expectedResult,
                                                                                  std::string externalDetails)
	{
		auto &db = mTestManager->getDB();
		auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
		auto expectedReviewableRequestAfterTx = reviewableRequestHelper->countObjects(db.getSession());

		auto referenceBeforeTx = ReferenceHelper::Instance()->loadReference(source.key.getPublicKey(), reference,
																			db);

		auto assetBeforeTx = AssetHelperLegacy::Instance()->loadAsset(assetCode, db);

        auto issuanceRequest = createIssuanceRequest(assetCode, amount, receiver, externalDetails);
        auto txFrame = createIssuanceRequestTx(source, issuanceRequest, reference, allTasks);

        auto reviewIssuanceChecker = ReviewIssuanceChecker(mTestManager, std::make_shared<IssuanceRequest>(issuanceRequest));

		mTestManager->applyCheck(txFrame);
		auto txResult = txFrame->getResult();
		auto opResult = txResult.result.results()[0];
		auto actualResultCode = CreateIssuanceRequestOpFrame::getInnerCode(opResult);
		REQUIRE(actualResultCode == expectedResult);

		uint64 reviewableRequestCountAfterTx = reviewableRequestHelper->countObjects(db.getSession());
		if (expectedResult != CreateIssuanceRequestResultCode::SUCCESS)
		{
			REQUIRE(expectedReviewableRequestAfterTx == reviewableRequestCountAfterTx);
			return CreateIssuanceRequestResult{};
		}

		REQUIRE(!referenceBeforeTx);
		auto result = opResult.tr().createIssuanceRequestResult();
		if (!result.success().fulfilled) {
			expectedReviewableRequestAfterTx++;
		}

		REQUIRE(expectedReviewableRequestAfterTx == reviewableRequestCountAfterTx);
		// if request was auto fulfilled, lets check if receiver actually got assets
		if (result.success().fulfilled) {
            reviewIssuanceChecker.checkApprove(nullptr);
			return result;
		}

		if (allTasks == nullptr)
		{
			return result;
		}

		auto requestID = result.success().requestID;
		auto issuanceRequestFrameAfterTx = ReviewableRequestHelper::Instance()->loadRequest(requestID, db);
		REQUIRE(!!issuanceRequestFrameAfterTx);

		auto& issuanceRequestEntryAfterTx = issuanceRequestFrameAfterTx->getRequestEntry();
		REQUIRE(issuanceRequestEntryAfterTx.ext.v() == LedgerVersion::ADD_TASKS_TO_REVIEWABLE_REQUEST);
		REQUIRE(issuanceRequestEntryAfterTx.ext.tasksExt().allTasks != 0);
		REQUIRE(issuanceRequestEntryAfterTx.ext.tasksExt().pendingTasks != 0);


		return result;
	}

	TransactionFramePtr
    IssuanceRequestHelper::createIssuanceRequestTx(Account &source, const IssuanceRequest &request,
												   std::string reference, uint32_t* allTasks)
	{
		Operation op;
		op.body.type(OperationType::CREATE_ISSUANCE_REQUEST);
		CreateIssuanceRequestOp& createIssuanceRequestOp = op.body.createIssuanceRequestOp();
		createIssuanceRequestOp.request = request;
        createIssuanceRequestOp.reference = reference;
		createIssuanceRequestOp.ext.v(LedgerVersion::ADD_TASKS_TO_REVIEWABLE_REQUEST);

        if (allTasks != nullptr)
		{
			createIssuanceRequestOp.ext.allTasks().activate() = *allTasks;
		}

		return txFromOperation(source, op, nullptr);
	}

    IssuanceRequest
    IssuanceRequestHelper::createIssuanceRequest(AssetCode assetCode, uint64_t amount, BalanceID receiver, std::string externalDetails)
    {
        IssuanceRequest issuanceRequest;
        issuanceRequest.amount = amount;
        issuanceRequest.asset = assetCode;
        issuanceRequest.receiver = receiver;
        issuanceRequest.externalDetails = externalDetails;
        issuanceRequest.ext.v(LedgerVersion::EMPTY_VERSION);
        return issuanceRequest;
    }

	void IssuanceRequestHelper::createAssetWithPreIssuedAmount(Account & assetOwner, AssetCode assetCode, uint64_t preIssuedAmount, Account& root) {
		auto manageAssetHelper = ManageAssetTestHelper(mTestManager);
		auto policies = assetOwner.key.getPublicKey() == root.key.getPublicKey()
														 ? static_cast<uint32_t>(AssetPolicy::BASE_ASSET)
														 : 0;
		manageAssetHelper.createAsset(assetOwner, assetOwner.key, assetCode, root, policies);
		authorizePreIssuedAmount(assetOwner, assetOwner.key, assetCode, preIssuedAmount, root);
	}

	void IssuanceRequestHelper::authorizePreIssuedAmount(Account &assetOwner, SecretKey &preIssuedAssetSigner,
                                                         AssetCode assetCode, uint64_t preIssuedAmount, Account &root)
	{
		auto preIssuanceResult = applyCreatePreIssuanceRequest(assetOwner, preIssuedAssetSigner, assetCode, preIssuedAmount,
			SecretKey::random().getStrKeyPublic());
        if (assetOwner.key == getRoot())
            return;
		auto reviewPreIssuanceRequestHelper = ReviewPreIssuanceRequestHelper(mTestManager);
		reviewPreIssuanceRequestHelper.applyReviewRequestTx(root, preIssuanceResult.success().requestID, ReviewRequestOpAction::APPROVE, "");
	}
}

}
