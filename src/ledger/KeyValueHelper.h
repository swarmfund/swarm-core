#pragma once

#include "ledger/EntryHelper.h"
#include "ledger/LedgerManager.h"
#include "ledger/KeyValueEntryFrame.h"

namespace soci
{
    class session;
}

namespace stellar
{

    class StatementContext;

    class KeyValueHelper : public EntryHelper {

    public:

        virtual KeyValueEntryFrame::pointer
        loadKeyValue(string256 valueKey) = 0;

        virtual void loadKeyValues(StatementContext &prep, std::function<void(LedgerEntry const &)> keyValueProcessor) = 0;
    };

}
