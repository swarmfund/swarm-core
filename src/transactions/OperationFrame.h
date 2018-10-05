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
class StorageHelper;

class TransactionFrame;

class OperationFrame
{

  private:
	bool checkCounterparties(Application& app, std::unordered_map<AccountID, CounterpartyDetails>& counterparties);
	bool checkRolePermissions(Application& app);
  
  protected:

    Operation const& mOperation;
    TransactionFrame& mParentTx;
    AccountFrame::pointer mSourceAccount;
    OperationResult& mResult;

	// checks signature, if not valid - returns false and sets operation error code;
    bool doCheckSignature(Application& app, Database& db, SourceDetails& sourceDetails);

    virtual bool doCheckValid(Application& app) = 0;
    virtual bool doApply(Application& app, LedgerDelta& delta,
                         LedgerManager& ledgerManager);
    virtual bool doApply(Application& app, StorageHelper& storageHelper,
                         LedgerManager& ledgerManager);

  public:
    virtual ~OperationFrame() = default;

    virtual std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(Database& db, LedgerDelta* delta) const = 0;
    virtual std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(Database& db, LedgerDelta* delta,
                                                                                      int32_t ledgerVersion) const;
    virtual SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                                      int32_t ledgerVersion) const = 0;
    virtual SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
        int32_t ledgerVersion, Database& db) const;

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

    void
    createReferenceEntry(std::string reference, StorageHelper& storageHelper);

    OperationResult&
    getResult() const
    {
        return mResult;
    }
    OperationResultCode getResultCode() const;

    bool checkValid(Application& app, LedgerDelta* delta = nullptr);

    bool apply(StorageHelper& storageHelper, Application& app);

    Operation const&
    getOperation() const
    {
        return mOperation;
    }

	virtual std::string getInnerResultCodeAsStr();
};
}
