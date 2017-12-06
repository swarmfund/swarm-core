// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "IssuanceRequestHelper.h"
#include "ledger/AssetHelper.h"
#include "ledger/BalanceHelper.h"
#include "ledger/ReviewableRequestFrame.h"
#include "ledger/ReviewableRequestHelper.h"
#include "ledger/ReferenceFrame.h"
#include "ledger/ReferenceHelper.h"
#include "transactions/issuance/CreatePreIssuanceRequestOpFrame.h"
#include "transactions/issuance/CreateIssuanceRequestOpFrame.h"
#include "ReviewPreIssuanceRequestHelper.h"
#include "ReviewIssuanceRequestHelper.h"
#include "ManageAssetHelper.h"


namespace stellar
{

namespace txtest
{
	IssuanceRequestHelper::IssuanceRequestHelper(TestManager::pointer testManager) : TxHelper(testManager)
	{
	}
	CreatePreIssuanceRequestResult IssuanceRequestHelper::applyCreatePreIssuanceRequest(Account & source, SecretKey & preIssuedAssetSigner, AssetCode assetCode, uint64_t amount, std::string reference, CreatePreIssuanceRequestResultCode expectedResult)
	{
		auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
		auto reviewableRequestCountBeforeTx = reviewableRequestHelper->countObjects(mTestManager->getDB().getSession());

		auto referenceHelper = ReferenceHelper::Instance();
		auto referenceBeforeTx = referenceHelper->loadReference(source.key.getPublicKey(), reference, mTestManager->getDB());
		auto txFrame = createPreIssuanceRequest(source, preIssuedAssetSigner, assetCode, amount, reference);
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

		REQUIRE(!referenceBeforeTx);
		REQUIRE(reviewableRequestCountBeforeTx + 1 == reviewableRequestCountAfterTx);
		return opResult.tr().createPreIssuanceRequestResult();
	}

	TransactionFramePtr IssuanceRequestHelper::createPreIssuanceRequest(Account & source, SecretKey & preIssuedAssetSigner, AssetCode assetCode, uint64_t amount, std::string reference)
	{
		Operation op;
		op.body.type(OperationType::CREATE_PREISSUANCE_REQUEST);
		CreatePreIssuanceRequestOp& createPreIssuanceRequestOp = op.body.createPreIssuanceRequest();
		createPreIssuanceRequestOp.request.amount = amount;
		createPreIssuanceRequestOp.request.asset = assetCode;
		createPreIssuanceRequestOp.request.reference = reference;
		createPreIssuanceRequestOp.request.signature = createPreIssuanceRequestSignature(preIssuedAssetSigner, assetCode, amount, reference);
		createPreIssuanceRequestOp.request.ext.v(LedgerVersion::EMPTY_VERSION);
		createPreIssuanceRequestOp.ext.v(LedgerVersion::EMPTY_VERSION);
		return txFromOperation(source, op, nullptr);
	}

	DecoratedSignature IssuanceRequestHelper::createPreIssuanceRequestSignature(SecretKey & preIssuedAssetSigner, AssetCode assetCode, uint64_t amount, std::string reference)
	{
		auto signatureData = CreatePreIssuanceRequestOpFrame::getSignatureData(reference, amount, assetCode);
		DecoratedSignature sig;
		sig.signature = preIssuedAssetSigner.sign(signatureData);
		sig.hint = PubKeyUtils::getHint(preIssuedAssetSigner.getPublicKey());
		return sig;
	}
	CreateIssuanceRequestResult IssuanceRequestHelper::applyCreateIssuanceRequest(Account & source, AssetCode assetCode, uint64_t amount,
		BalanceID receiver, std::string reference, CreateIssuanceRequestResultCode expectedResult)
	{
		auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
		auto expectedReviewableRequestAfterTx = reviewableRequestHelper->countObjects(mTestManager->getDB().getSession());

		auto referenceHelper = ReferenceHelper::Instance();
		auto referenceBeforeTx = referenceHelper->loadReference(source.key.getPublicKey(), reference, mTestManager->getDB());

		auto assetHelper = AssetHelper::Instance();
		auto assetBeforeTx = assetHelper->loadAsset(assetCode, mTestManager->getDB());

		auto balanceHelper = BalanceHelper::Instance();
		auto balanceBeforeTx = balanceHelper->loadBalance(receiver, mTestManager->getDB());

		auto txFrame = createIssuanceRequest(source, assetCode, amount, receiver, reference);
		mTestManager->applyCheck(txFrame);
		auto txResult = txFrame->getResult();
		auto opResult = txResult.result.results()[0];
		auto actualResultCode = CreateIssuanceRequestOpFrame::getInnerCode(opResult);
		REQUIRE(actualResultCode == expectedResult);

		uint64 reviewableRequestCountAfterTx = reviewableRequestHelper->countObjects(mTestManager->getDB().getSession());
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
			auto reviewIssuanceRequestHelper = ReviewIssuanceRequestHelper(mTestManager);
			// as we do not have stored issuance request, we need to get it from tx
			auto issuanceRequest = txFrame->getEnvelope().tx.operations[0].body.createIssuanceRequestOp().request;
			reviewIssuanceRequestHelper.checkApproval(issuanceRequest, assetBeforeTx, balanceBeforeTx);
		}

		return result;
	}
	TransactionFramePtr IssuanceRequestHelper::createIssuanceRequest(Account & source, AssetCode assetCode, uint64_t amount, BalanceID receiver, std::string reference)
	{
		Operation op;
		op.body.type(OperationType::CREATE_ISSUANCE_REQUEST);
		CreateIssuanceRequestOp& createIssuanceRequestOp = op.body.createIssuanceRequestOp();
		createIssuanceRequestOp.request.amount = amount;
		createIssuanceRequestOp.request.asset = assetCode;
		createIssuanceRequestOp.reference = reference;
		createIssuanceRequestOp.request.receiver = receiver;
		return txFromOperation(source, op, nullptr);
	}
	void IssuanceRequestHelper::createAssetWithPreIssuedAmount(Account & assetOwner, AssetCode assetCode, uint64_t preIssuedAmount, Account& root) {
		auto manageAssetHelper = ManageAssetHelper(mTestManager);
		manageAssetHelper.createAsset(assetOwner, assetOwner.key, assetCode, root);
		authorizePreIssuedAmount(assetOwner, assetOwner, assetCode, preIssuedAmount, root);
	}
	void IssuanceRequestHelper::authorizePreIssuedAmount(Account & assetOwner, Account & preIssuedAssetSigner, AssetCode assetCode, uint64_t preIssuedAmount, Account & root)
	{
		auto preIssuanceResult = applyCreatePreIssuanceRequest(assetOwner, preIssuedAssetSigner.key, assetCode, preIssuedAmount,
			SecretKey::random().getStrKeyPublic());
		auto reviewPreIssuanceRequestHelper = ReviewPreIssuanceRequestHelper(mTestManager);
		reviewPreIssuanceRequestHelper.applyReviewRequestTx(root, preIssuanceResult.success().requestID, ReviewRequestOpAction::APPROVE, "");
	}
}

}
