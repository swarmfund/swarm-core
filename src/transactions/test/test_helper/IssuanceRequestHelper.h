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

		CreatePreIssuanceRequestResult applyCreatePreIssuanceRequest(Account& source, SecretKey& preIssuedAssetSigner,
                                                          AssetCode assetCode, uint64_t amount, std::string reference,
                      CreatePreIssuanceRequestResultCode expectedResult = CreatePreIssuanceRequestResultCode::SUCCESS);

		TransactionFramePtr createPreIssuanceRequestTx(Account &source, const PreIssuanceRequest &request);

        PreIssuanceRequest createPreIssuanceRequest(SecretKey &preIssuedAssetSigner, AssetCode assetCode, uint64_t amount,
                                                    std::string reference);

		static DecoratedSignature createPreIssuanceRequestSignature(SecretKey& preIssuedAssetSigner, AssetCode assetCode,
                                                                    uint64_t amount, std::string reference);

		CreateIssuanceRequestResult applyCreateIssuanceRequest(Account& source, AssetCode assetCode, uint64_t amount, BalanceID receiver,
			std::string reference, uint32_t *allTasks = nullptr,
		    CreateIssuanceRequestResultCode expectedResult = CreateIssuanceRequestResultCode::SUCCESS,
		    std::string externalDetails = "{}");

		TransactionFramePtr createIssuanceRequestTx(Account &source, const IssuanceRequest &request,
													std::string reference, uint32_t *allTasks = nullptr);

        IssuanceRequest createIssuanceRequest(AssetCode assetCode, uint64_t amount, BalanceID receiver,
                                              std::string externalDetails = "{}");




		void createAssetWithPreIssuedAmount(Account& assetOwner, AssetCode assetCode, uint64_t preIssuedAmount, Account& root);

		void authorizePreIssuedAmount(Account &assetOwner, SecretKey &preIssuedAssetSigner, AssetCode assetCode,
                                      uint64_t preIssuedAmount, Account &root);
	};
}
}
