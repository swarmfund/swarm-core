// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <transactions/test/TxTests.h>
#include "ManageAssetHelper.h"
#include "ledger/ReviewableRequestFrame.h"
#include "transactions/manage_asset/ManageAssetOpFrame.h"
#include "ReviewAssetRequestHelper.h"


namespace stellar
{

namespace txtest
{
	ManageAssetHelper::ManageAssetHelper(TestManager::pointer testManager) : TxHelper(testManager)
	{
	}

	ManageAssetResult ManageAssetHelper::applyManageAssetTx(Account & source, uint64_t requestID, ManageAssetOp::_request_t request, ManageAssetResultCode expectedResult)
	{
		auto reviewableRequestCountBeforeTx = ReviewableRequestFrame::countObjects(mTestManager->getDB().getSession());
		LedgerDelta& delta = mTestManager->getLedgerDelta();
		auto requestBeforeTx = ReviewableRequestFrame::loadRequest(requestID, mTestManager->getLedgerManager().getDatabase(), &delta);
		auto txFrame = createManageAssetTx(source, requestID, request);

		mTestManager->applyCheck(txFrame);
		auto txResult = txFrame->getResult();
		auto opResult = txResult.result.results()[0];
		auto actualResultCode = ManageAssetOpFrame::getInnerCode(opResult);
		REQUIRE(actualResultCode == expectedResult);

        uint64 reviewableRequestCountAfterTx = ReviewableRequestFrame::countObjects(mTestManager->getDB().getSession());
        if (expectedResult != ManageAssetResultCode::SUCCESS)
        {
            REQUIRE(reviewableRequestCountBeforeTx == reviewableRequestCountAfterTx);
            return ManageAssetResult{};
        }

        auto sourceFrame = AccountFrame::loadAccount(source.key.getPublicKey(), mTestManager->getDB());
        auto manageAssetResult = opResult.tr().manageAssetResult();
        if (sourceFrame->getAccountType() == AccountType::MASTER) {
            REQUIRE(reviewableRequestCountAfterTx == reviewableRequestCountBeforeTx);
            REQUIRE(manageAssetResult.success().fulfilled);
            AssetCode assetCode;
            switch (request.action()) {
                case ManageAssetAction::CREATE_ASSET_CREATION_REQUEST:
                    assetCode = request.createAsset().code;
                    break;
                case ManageAssetAction::CREATE_ASSET_UPDATE_REQUEST:
                {
                    assetCode = request.updateAsset().code;
                    auto assetFrame = AssetFrame::loadAsset(assetCode, mTestManager->getDB());
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
            auto assetFrame = AssetFrame::loadAsset(assetCode, mTestManager->getDB());
            REQUIRE(assetFrame);
            if (assetFrame->checkPolicy(AssetPolicy::BASE_ASSET)) {
                auto systemAccounts = mTestManager->getApp().getSystemAccounts();
                for (auto systemAccount : systemAccounts) {
                    auto balanceFrame = BalanceFrame::loadBalance(systemAccount, assetCode,
                                                                  mTestManager->getDB(), &delta);
                    REQUIRE(balanceFrame);
                }
            }
            return manageAssetResult;
        }

        const bool isUpdatingExistingRequest = requestID != 0;
        if (isUpdatingExistingRequest) {
            REQUIRE(!!requestBeforeTx);
        }

		auto requestAfterTx = ReviewableRequestFrame::loadRequest(manageAssetResult.success().requestID, mTestManager->getDB(), &delta);
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

	TransactionFramePtr ManageAssetHelper::createManageAssetTx(Account & source, uint64_t requestID, ManageAssetOp::_request_t request)
	{
		Operation op;
		op.body.type(OperationType::MANAGE_ASSET);
		ManageAssetOp& manageAssetOp = op.body.manageAssetOp();
		manageAssetOp.requestID = requestID;
		manageAssetOp.request = request;
		return txFromOperation(source, op, nullptr);
	}

	ManageAssetOp::_request_t ManageAssetHelper::createAssetCreationRequest(AssetCode code, std::string name, AccountID preissuedAssetSigner, std::string description, std::string externalResourceLink, uint64_t maxIssuanceAmount, uint32_t policies)
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
		return request;
	}

	ManageAssetOp::_request_t ManageAssetHelper::createAssetUpdateRequest(AssetCode code, std::string description, std::string externalResourceLink, uint32_t policies)
	{
		ManageAssetOp::_request_t request;
		request.action(ManageAssetAction::CREATE_ASSET_UPDATE_REQUEST);
		AssetUpdateRequest& assetUpdateRequest = request.updateAsset();
		assetUpdateRequest.code = code;
		assetUpdateRequest.description = description;
		assetUpdateRequest.externalResourceLink = externalResourceLink;
		assetUpdateRequest.policies = policies;
		return request;
	}

	ManageAssetOp::_request_t ManageAssetHelper::createCancelRequest()
	{
		ManageAssetOp::_request_t request;
		request.action(ManageAssetAction::CANCEL_ASSET_REQUEST);
		return request;
	}
	void ManageAssetHelper::createAsset(Account &assetOwner, SecretKey &preIssuedSigner, AssetCode assetCode, Account &root)
	{
		auto creationRequest = createAssetCreationRequest(assetCode, "New token", preIssuedSigner.getPublicKey(),
			"Description can be quiete long", "https://testusd.usd", UINT64_MAX, 0);
        auto creationResult = applyManageAssetTx(assetOwner, 0, creationRequest);

        auto assetOwnerFrame = AccountFrame::loadAccount(assetOwner.key.getPublicKey(), mTestManager->getDB());
        if (assetOwnerFrame->getAccountType() == AccountType::MASTER)
            return;

        LedgerDelta& delta = mTestManager->getLedgerDelta();
        auto approvingRequest = ReviewableRequestFrame::loadRequest(creationResult.success().requestID, mTestManager->getDB(), &delta);
		REQUIRE(approvingRequest);
		auto reviewRequetHelper = ReviewAssetRequestHelper(mTestManager);
		reviewRequetHelper.applyReviewRequestTx(root, approvingRequest->getRequestID(), approvingRequest->getHash(), approvingRequest->getType(),
			ReviewRequestOpAction::APPROVE, "");
	}

    void ManageAssetHelper::createBaseAsset(Account &root, SecretKey &preIssuedSigner, AssetCode assetCode)
    {
        uint32 baseAssetPolicy = static_cast<uint32>(AssetPolicy::BASE_ASSET);
        auto creationRequest = createAssetCreationRequest(assetCode, "New token", preIssuedSigner.getPublicKey(),
              "Description can be quiete long", "https://testusd.usd", UINT64_MAX, baseAssetPolicy);
        auto creationResult = applyManageAssetTx(root, 0, creationRequest);
    }
}

}
