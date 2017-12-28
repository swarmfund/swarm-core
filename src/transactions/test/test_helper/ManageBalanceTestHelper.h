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
	class ManageBalanceTestHelper : TxHelper
	{
	public:
		explicit ManageBalanceTestHelper(TestManager::pointer testManager);

		TransactionFramePtr createManageBalanceTx(Account& from, PublicKey& account, AssetCode asset, 
												  ManageBalanceAction action = ManageBalanceAction::CREATE, 
												  Account* signer = nullptr);

		ManageBalanceResult applyManageBalanceTx(Account& from, PublicKey& account, AssetCode asset,
												 ManageBalanceAction action = ManageBalanceAction::CREATE, 
												 ManageBalanceResultCode expectedResultCode = 
												 ManageBalanceResultCode::SUCCESS, Account* signer = nullptr);

		void createBalance(Account& from, PublicKey& account, AssetCode asset);

	};
}

}