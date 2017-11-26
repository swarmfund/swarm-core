#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <memory>
#include "transactions/AccountManager.h"
#include "transactions/CounterpartyDetails.h"
#include "transactions/SourceDetails.h"
#include "ledger/LedgerManager.h"
#include "ledger/AccountFrame.h"
#include "ledger/AssetFrame.h"
#include "ledger/BalanceFrame.h"
#include "overlay/StellarXDR.h"
#include "util/types.h"
#include "ledger/FeeFrame.h"

namespace medida
{
class MetricsRegistry;
}

namespace stellar
{
class Application;
class LedgerManager;
class LedgerDelta;

class TransactionFrame;

class OperationFrame
{

  private:
	bool checkCounterparties(Application& app, std::unordered_map<AccountID, CounterpartyDetails>& counterparties);
  
  protected:

    Operation const& mOperation;
    TransactionFrame& mParentTx;
    AccountFrame::pointer mSourceAccount;
    OperationResult& mResult;

	// checks signature, if not valid - returns false and sets operation error code;
    bool doCheckSignature(Application& app, Database& db, SourceDetails& sourceDetails);

    virtual bool doCheckValid(Application& app) = 0;
    virtual bool doApply(Application& app, LedgerDelta& delta,
                         LedgerManager& ledgerManager) = 0;

  public:

	  virtual std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(Database& db, LedgerDelta* delta) const = 0;
	  virtual SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const = 0;

	// returns true if operation is allowed in the system
	virtual bool isAllowed() const;

	// returns fee paid for operation.
	// default fee for all operations is 0, finantial operations must override this function
    virtual int64_t getPaidFee() const;

    static std::shared_ptr<OperationFrame>
    makeHelper(Operation const& op, OperationResult& res,
               TransactionFrame& parentTx);

    OperationFrame(Operation const& op, OperationResult& res,
                   TransactionFrame& parentTx);
    OperationFrame(OperationFrame const&) = delete;

    AccountFrame&
    getSourceAccount() const
    {
        assert(mSourceAccount);
        return *mSourceAccount;
    }

    // overrides internal sourceAccount used by this operation
    // normally set automatically by checkValid
    void
    setSourceAccountPtr(AccountFrame::pointer sa)
    {
        mSourceAccount = sa;
    }
    
    AccountID const& getSourceID() const;

    // load account if needed
    // returns true on success
    bool loadAccount(LedgerDelta* delta, Database& db);



    PaymentRequestEntry
    createPaymentRequest(uint64 paymentID, BalanceID sourceBalance, int64 sourceSend,
            int64 sourceSendUniversal,
            BalanceID* destBalance, int64 destReceive, LedgerDelta& delta,
            Database& db, uint64 createdAt, uint64* invoiceID = nullptr);

    void
    createReferenceEntry(std::string reference, LedgerDelta* delta, Database& db);

    OperationResult&
    getResult() const
    {
        return mResult;
    }
    OperationResultCode getResultCode() const;

    bool checkValid(Application& app, LedgerDelta* delta = nullptr);

    bool apply(LedgerDelta& delta, Application& app);

    Operation const&
    getOperation() const
    {
        return mOperation;
    }

	virtual std::string getInnerResultCodeAsStr();
};
}
