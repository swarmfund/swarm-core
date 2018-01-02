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

    class CreateAccountTestBuilder;

    template<typename SpecificBuilder>
    class OperationBuilder {
    public:
        virtual Operation buildOp() = 0;

        TransactionFramePtr buildTx(TestManager::pointer testManager);

        SpecificBuilder setOperationResultCode(OperationResultCode operationResultCode);
        Account *signer = nullptr;
        Account source;
        OperationResultCode operationResultCode;
    };

	class TxHelper
	{
	protected:
		TestManager::pointer mTestManager;

    public:

        TransactionFramePtr txFromOperation(Account& account, Operation const& op, Account* signer = nullptr);
        TxHelper(TestManager::pointer testManager);

	};
}
}
