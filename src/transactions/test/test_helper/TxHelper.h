#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "overlay/StellarXDR.h"
#include "TestManager.h"
#include "Account.h"
#include "transactions/TransactionFrame.h"
#include "transactions/test/TxTests.h"
#include "TestUtils.h"

namespace stellar
{
namespace txtest 
{	
	class TxHelper
	{
	protected:
		TestManager::pointer mTestManager;

		TransactionFramePtr txFromOperation(Account& account, Operation const& op, Account* signer = nullptr);
	public:
		TxHelper(TestManager::pointer testManager);

		void applyAndCheckFirstOperation(OperationResultCode expectedResult){
			auto operation = buildTx();
            mTestManager->applyCheck(operation);
            auto actualResult = getFirstResult(*operation).code();
            mustEqualsResultCode<OperationResultCode>(expectedResult, actualResult);
		}

        virtual TransactionFramePtr buildTx() { ; }
	};
}
}
