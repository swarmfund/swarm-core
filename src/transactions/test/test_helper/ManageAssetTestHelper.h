#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "overlay/StellarXDR.h"
#include "TxHelper.h"

namespace stellar
{
namespace txtest 
{	
	class ManageAssetTestHelper : TxHelper
	{
    private:
        void validateManageAssetEffect(ManageAssetOp::_request_t request);
	public:
		ManageAssetTestHelper(TestManager::pointer testManager);

		void createApproveRequest(Account& root, Account & source, ManageAssetOp::_request_t request);

		ManageAssetResult applyManageAssetTx(Account & source, uint64_t requestID, ManageAssetOp::_request_t request,
			ManageAssetResultCode expectedResult = ManageAssetResultCode::SUCCESS, OperationResultCode expectedOpCode = OperationResultCode::opINNER);
		TransactionFramePtr createManageAssetTx(Account& source, uint64_t requestID, ManageAssetOp::_request_t request);

		Operation createManageAssetOp(Account& source, uint64_t requestID, ManageAssetOp::_request_t request);

		ManageAssetOp::_request_t createAssetCreationRequest(
				AssetCode code,
				AccountID preissuedAssetSigner,
				std::string details,
				uint64_t maxIssuanceAmount,
				uint32_t policies,
                uint64_t initialPreissuanceAmount = 0);

		ManageAssetOp::_request_t createAssetUpdateRequest(
				AssetCode code,
				std::string details,
				uint32_t policies);

		ManageAssetOp::_request_t createCancelRequest();

                ManageAssetOp::_request_t updateMaxAmount(AssetCode asset, uint64 amount);

                ManageAssetOp::_request_t createChangeSignerRequest(
                    AssetCode code,
                    AccountID accountID);

		void createAsset(Account &assetOwner, SecretKey &preIssuedSigner, AssetCode assetCode, Account &root, uint32_t policies);
                void updateAsset(Account& assetOwner, AssetCode assetCode, Account& root, uint32_t policies);
	};
}
}
