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
	class ManageAssetHelper : TxHelper
	{
	public:
		ManageAssetHelper(TestManager::pointer testManager);

		ManageAssetResult applyManageAssetTx(Account & source, uint64_t requestID, ManageAssetOp::_request_t request,
			ManageAssetResultCode expectedResult = ManageAssetResultCode::SUCCESS);
		TransactionFramePtr createManageAssetTx(Account& source, uint64_t requestID, ManageAssetOp::_request_t request);

		ManageAssetOp::_request_t createAssetCreationRequest(AssetCode code, std::string name, AccountID preissuedAssetSigner,
			std::string description, std::string externalResourceLink, uint64_t maxIssuanceAmount, uint32_t policies);
		ManageAssetOp::_request_t createAssetUpdateRequest(AssetCode code, std::string description, std::string externalResourceLink, 
			uint32_t policies);
		ManageAssetOp::_request_t createCancelRequest();

		void createAsset(Account &assetOwner, SecretKey &preIssuedSigner, AssetCode assetCode, Account &root);

		void createBaseAsset(Account &root, SecretKey &preIssuedSigner, AssetCode assetCode);
	};
}
}
