#pragma once

#include "ManageExternalSystemAccountIDPoolEntryOpFrame.h"

namespace stellar
{

class CreateExternalSystemAccountIDPoolEntryOpFrame : public ManageExternalSystemAccountIdPoolEntryOpFrame
{
    CreateExternalSystemAccountIdPoolEntryActionInput const& mInput;

public:
    CreateExternalSystemAccountIDPoolEntryOpFrame(Operation const& op, OperationResult& res,
                                                  TransactionFrame& parentTx);

    bool doApply(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager) override;
    bool doCheckValid(Application& app) override;
};

}