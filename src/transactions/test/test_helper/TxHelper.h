#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "overlay/StellarXDR.h"
#include "TestManager.h"
#include "Account.h"
#include "transactions/TransactionFrame.h"
#include <vector>

namespace stellar
{
namespace txtest 
{

    template<typename SpecificBuilder>
    class OperationBuilder {
    public:
        virtual Operation buildOp() = 0;
        virtual SpecificBuilder copy() = 0;

        TransactionFramePtr buildTx(TestManager::pointer testManager) {
            Transaction tx;
            tx.sourceAccount = source.key.getPublicKey();
            tx.salt = source.getNextSalt();
            tx.operations.push_back(buildOp());
            tx.timeBounds.minTime = 0;
            tx.timeBounds.maxTime = INT64_MAX / 2;

            TransactionEnvelope envelope;
            envelope.tx = tx;
            auto res = TransactionFrame::makeTransactionFromWire(testManager->getNetworkID(), envelope);

            if (signer == nullptr) {
                signer = &source;
            }
            res->addSignature(signer->key);

            return res;
        }

        SpecificBuilder setOperationResultCode(OperationResultCode operationResultCode){
            SpecificBuilder specificBuilder = copy();
            specificBuilder.operationResultCode = operationResultCode;
            return specificBuilder;
        }

        SpecificBuilder setSource(Account from) {
            SpecificBuilder newTestHelper = copy();
            newTestHelper.source = from;
            return newTestHelper;
        }

        SpecificBuilder setSigner(Account *signer) {
            SpecificBuilder newTestHelper = copy();
            newTestHelper.signer = signer;
            return newTestHelper;
        }

        Account *signer = nullptr;
        Account source;
        OperationResultCode operationResultCode = OperationResultCode::opINNER;
    };

	class TxHelper
	{
	protected:
		TestManager::pointer mTestManager;

    public:

        TransactionFramePtr txFromOperation(Account& account, Operation const& op, Account* signer = nullptr);
        TransactionFramePtr txFromOperations(Account& source, std::vector<Operation>& ops,
                                             uint64_t* maxTotalFee = nullptr, Account* signer = nullptr);
        TxHelper(TestManager::pointer testManager);

	};
}
}
