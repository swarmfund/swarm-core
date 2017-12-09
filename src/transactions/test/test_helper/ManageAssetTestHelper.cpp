// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <transactions/test/TxTests.h>
#include <cstdint>
#include "ManageAssetTestHelper.h"
#include "ledger/AccountHelper.h"
#include "ledger/AssetHelper.h"
#include "ledger/BalanceHelper.h"
#include "ledger/ReviewableRequestFrame.h"
#include "ledger/ReviewableRequestHelper.h"
#include "transactions/manage_asset/ManageAssetOpFrame.h"
#include "ReviewAssetRequestHelper.h"


namespace stellar
{

namespace txtest
{
	ManageAssetTestHelper::ManageAssetTestHelper(TestManager::pointer testManager) : TxHelper(testManager)
	{
	}

	ManageAssetResult ManageAssetTestHelper::applyManageAssetTx(Account & source, uint64_t requestID, ManageAssetOp::_request_t request, ManageAssetResultCode expectedResult)
	{
		auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
		auto reviewableRequestCountBeforeTx = reviewableRequestHelper->countObjects(mTestManager->getDB().getSession());
		LedgerDelta& delta = mTestManager->getLedgerDelta();
		auto requestBeforeTx = reviewableRequestHelper->loadRequest(requestID, mTestManager->getLedgerManager().getDatabase(), &delta);
		auto txFrame = createManageAssetTx(source, requestID, request);

		mTestManager->applyCheck(txFrame);
		auto txResult = txFrame->getResult();
		auto opResult = txResult.result.results()[0];
		auto actualResultCode = ManageAssetOpFrame::getInnerCode(opResult);
		REQUIRE(actualResultCode == expectedResult);

        uint64 reviewableRequestCountAfterTx = reviewableRequestHelper->countObjects(mTestManager->getDB().getSession());
        if (expectedResult != ManageAssetResultCode::SUCCESS)
        {
            REQUIRE(reviewableRequestCountBeforeTx == reviewableRequestCountAfterTx);
            return ManageAssetResult{};
        }

		auto accountHelper = AccountHelper::Instance();
        auto sourceFrame = accountHelper->loadAccount(source.key.getPublicKey(), mTestManager->getDB());
        auto manageAssetResult = opResult.tr().manageAssetResult();

		auto assetHelper = AssetHelper::Instance();
		auto balanceHelper = BalanceHelper::Instance();

        if (sourceFrame->getAccountType() == AccountType::MASTER) {
            REQUIRE(reviewableRequestCountAfterTx == reviewableRequestCountBeforeTx);
            REQUIRE(manageAssetResult.success().fulfilled);

            validateManageAssetEffect(request);

            return manageAssetResult;
        }

        const bool isUpdatingExistingRequest = requestID != 0;
        if (isUpdatingExistingRequest) {
            REQUIRE(!!requestBeforeTx);
        }

		auto requestAfterTx = reviewableRequestHelper->loadRequest(manageAssetResult.success().requestID, mTestManager->getDB(), &delta);
		if (request.action() == ManageAssetAction::CANCEL_ASSET_REQUEST) {
			REQUIRE(!requestAfterTx);
			return manageAssetResult;
		}

		REQUIRE(requestAfterTx);
		REQUIRE(requestAfterTx->getRequestEntry().rejectReason.empty());

		switch (request.action()) {
		case ManageAssetAction::CREATE_ASSET_CREATION_REQUEST:
			REQUIRE(requestAfterTx->getRequestEntry().body.assetCreationRequest() == request.createAsset());
			break;
		case  ManageAssetAction::CREATE_ASSET_UPDATE_REQUEST:
			REQUIRE(requestAfterTx->getRequestEntry().body.assetUpdateRequest() == request.updateAsset());
			break;
		default:
			throw std::runtime_error("Unexpected action for manage asset");
		}

		return manageAssetResult;
	}

	TransactionFramePtr ManageAssetTestHelper::createManageAssetTx(Account & source, uint64_t requestID, ManageAssetOp::_request_t request)
	{
		Operation op;
		op.body.type(OperationType::MANAGE_ASSET);
		ManageAssetOp& manageAssetOp = op.body.manageAssetOp();
		manageAssetOp.requestID = requestID;
		manageAssetOp.request = request;
		return txFromOperation(source, op, nullptr);
	}

	ManageAssetOp::_request_t ManageAssetTestHelper::createAssetCreationRequest(
			AssetCode code,
			std::string name,
			AccountID preissuedAssetSigner,
			std::string description,
			std::string externalResourceLink,
			uint64_t maxIssuanceAmount,
			uint32_t policies,
			std::string logoID)
	{
		ManageAssetOp::_request_t request;
		request.action(ManageAssetAction::CREATE_ASSET_CREATION_REQUEST);
		AssetCreationRequest& assetCreationRequest = request.createAsset();
		assetCreationRequest.code = code;
		assetCreationRequest.description = description;
		assetCreationRequest.externalResourceLink = externalResourceLink;
		assetCreationRequest.maxIssuanceAmount = maxIssuanceAmount;
		assetCreationRequest.name = name;
		assetCreationRequest.policies = policies;
		assetCreationRequest.preissuedAssetSigner = preissuedAssetSigner;
		assetCreationRequest.logoID = logoID;
		return request;
	}

	ManageAssetOp::_request_t ManageAssetTestHelper::createAssetUpdateRequest(
			AssetCode code,
			std::string description,
			std::string externalResourceLink,
			uint32_t policies,
			std::string logoID
	)
	{
		ManageAssetOp::_request_t request;
		request.action(ManageAssetAction::CREATE_ASSET_UPDATE_REQUEST);
		AssetUpdateRequest& assetUpdateRequest = request.updateAsset();
		assetUpdateRequest.code = code;
		assetUpdateRequest.description = description;
		assetUpdateRequest.externalResourceLink = externalResourceLink;
		assetUpdateRequest.policies = policies;
		assetUpdateRequest.logoID = logoID;
		return request;
	}

	ManageAssetOp::_request_t ManageAssetTestHelper::createCancelRequest()
	{
		ManageAssetOp::_request_t request;
		request.action(ManageAssetAction::CANCEL_ASSET_REQUEST);
		return request;
	}
	void ManageAssetTestHelper::createAsset(Account &assetOwner, SecretKey &preIssuedSigner, AssetCode assetCode, Account &root)
	{
		auto creationRequest = createAssetCreationRequest(assetCode, "New token", preIssuedSigner.getPublicKey(),
			"Description can be quiete long", "https://testusd.usd", UINT64_MAX, 0, "123");
        auto creationResult = applyManageAssetTx(assetOwner, 0, creationRequest);

		auto accountHelper = AccountHelper::Instance();
        auto assetOwnerFrame = accountHelper->loadAccount(assetOwner.key.getPublicKey(), mTestManager->getDB());
        if (assetOwnerFrame->getAccountType() == AccountType::MASTER)
            return;

        LedgerDelta& delta = mTestManager->getLedgerDelta();
		auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
        auto approvingRequest = reviewableRequestHelper->loadRequest(creationResult.success().requestID, mTestManager->getDB(), &delta);
		REQUIRE(approvingRequest);
		auto reviewRequetHelper = ReviewAssetRequestHelper(mTestManager);
		reviewRequetHelper.applyReviewRequestTx(root, approvingRequest->getRequestID(), approvingRequest->getHash(), approvingRequest->getType(),
			ReviewRequestOpAction::APPROVE, "");
	}

    void ManageAssetTestHelper::createBaseAsset(Account &root, SecretKey &preIssuedSigner, AssetCode assetCode)
    {
        uint32 baseAssetPolicy = static_cast<uint32>(AssetPolicy::BASE_ASSET);
        auto creationRequest = createAssetCreationRequest(assetCode, "New token", preIssuedSigner.getPublicKey(),
              "Description can be quiete long", "https://testusd.usd", UINT64_MAX, baseAssetPolicy, "123");
        auto creationResult = applyManageAssetTx(root, 0, creationRequest);
    }

    void ManageAssetTestHelper::validateManageAssetEffect(ManageAssetOp::_request_t request) {
        AssetCode assetCode;
        auto assetHelper = AssetHelper::Instance();
        switch (request.action()) {
            case ManageAssetAction::CREATE_ASSET_CREATION_REQUEST:
                assetCode = request.createAsset().code;
                break;
            case ManageAssetAction::CREATE_ASSET_UPDATE_REQUEST:
            {
                assetCode = request.updateAsset().code;
                auto assetFrame = assetHelper->loadAsset(assetCode, mTestManager->getDB());
                REQUIRE(assetFrame);
                auto assetEntry = assetFrame->getAsset();
                REQUIRE(assetEntry.description == request.updateAsset().description);
                REQUIRE(assetEntry.externalResourceLink == request.updateAsset().externalResourceLink);
                REQUIRE(assetEntry.policies == request.updateAsset().policies);
                break;
            }
            default:
                throw std::runtime_error("Unexpected manage asset action from master account");
        }
        auto assetFrame = assetHelper->loadAsset(assetCode, mTestManager->getDB());
        REQUIRE(assetFrame);
        auto balanceHelper = BalanceHelper::Instance();
        if (assetFrame->checkPolicy(AssetPolicy::BASE_ASSET)) {
            auto systemAccounts = mTestManager->getApp().getSystemAccounts();
            for (auto systemAccount : systemAccounts) {
                auto balanceFrame = balanceHelper->loadBalance(systemAccount, assetCode,
                                                              mTestManager->getDB(), &mTestManager->getLedgerDelta());
                REQUIRE(balanceFrame);
            }
        }
    }
}

}
