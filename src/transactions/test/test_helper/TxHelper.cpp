// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "TestManager.h"
#include "TxHelper.h"


namespace stellar
{

namespace txtest
{
	TransactionFramePtr TxHelper::txFromOperation(Account & source, Operation const & op, Account* signer)
	{
		Transaction tx;
		tx.sourceAccount = source.key.getPublicKey();
		tx.salt = source.getNextSalt();
		tx.operations.push_back(op);
		tx.timeBounds.minTime = 0;
		tx.timeBounds.maxTime = INT64_MAX / 2;

		TransactionEnvelope envelope;
		envelope.tx = tx;
		auto res = TransactionFrame::makeTransactionFromWire(mTestManager->getNetworkID(), envelope);

		if (signer == nullptr) {
			signer = &source;
		}
		res->addSignature(signer->key);

		return res;
	}

	TxHelper::TxHelper(TestManager::pointer testManager)
	{
		mTestManager = testManager;
	}

}

}
