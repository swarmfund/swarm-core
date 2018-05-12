#pragma once

#include "ManageExternalSystemAccountIDPoolEntryOpFrame.h"

namespace stellar
{

class DeleteExternalSystemAccountIDPoolEntryOpFrame : public ManageExternalSystemAccountIdPoolEntryOpFrame
{
    DeleteExternalSystemAccountIdPoolEntryActionInput const& mInput;

public:
    DeleteExternalSystemAccountIDPoolEntryOpFrame(Operation const& op, OperationResult& res,
                                                  TransactionFrame& parentTx);

    bool doApply(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager) override;
    bool doCheckValid(Application& app) override;
};

}
