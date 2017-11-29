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
	class IssuanceRequestHelper : TxHelper
	{
	public:
		IssuanceRequestHelper(TestManager::pointer testManager);

		CreatePreIssuanceRequestResult applyCreatePreIssuanceRequest(Account& source, SecretKey& preIssuedAssetSigner, AssetCode assetCode, uint64_t amount,
			std::string reference, CreatePreIssuanceRequestResultCode expectedResult = CreatePreIssuanceRequestResultCode::SUCCESS);
		TransactionFramePtr createPreIssuanceRequest(Account& source, SecretKey& preIssuedAssetSigner, AssetCode assetCode, uint64_t amount,
			std::string reference);

		static DecoratedSignature createPreIssuanceRequestSignature(SecretKey& preIssuedAssetSigner, AssetCode assetCode, uint64_t amount,
			std::string reference);

		CreateIssuanceRequestResult applyCreateIssuanceRequest(Account& source, AssetCode assetCode, uint64_t amount, BalanceID receiver,
			std::string reference, CreateIssuanceRequestResultCode expectedResult = CreateIssuanceRequestResultCode::SUCCESS);
		TransactionFramePtr createIssuanceRequest(Account& source, AssetCode assetCode, uint64_t amount, BalanceID receiver, std::string reference);


		void createAssetWithPreIssuedAmount(Account& assetOwner, AssetCode assetCode, uint64_t preIssuedAmount, Account& root);

		void authorizePreIssuedAmount(Account& assetOwner, Account& preIssuedAssetSigner, AssetCode assetCode, uint64_t preIssuedAmount, Account& root);
	};
}
}
