
#include "ManageKeyValueOpFrame.h"
#include "ledger/LedgerDelta.h"
#include "ledger/KeyValueHelper.h"
#include "database/Database.h"
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"


namespace stellar {
    using namespace std;
    using xdr::operator==;

    ManageKeyValueOpFrame::ManageKeyValueOpFrame(const stellar::Operation &op, stellar::OperationResult &res,
                                                 stellar::TransactionFrame &parentTx)
            : OperationFrame(op, res, parentTx),
              mManageKeyValue(mOperation.body.manageKeyValueOp())
    {
    }

    bool ManageKeyValueOpFrame::doApply(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager) {
        Database &db = ledgerManager.getDatabase();
        auto keyValueHelper = KeyValueHelper::Instance();
        auto keyValueFrame = keyValueHelper->loadKeyValue(this->mManageKeyValue.key, db, &delta);

        if (mManageKeyValue.action.action() == ManageKVAction::DELETE) {
            if (!keyValueFrame) {
                innerResult().code(ManageKeyValueResultCode::NOT_FOUND);
                return false;
            }

            auto ledgerKey = keyValueFrame->getKey();
            keyValueHelper->storeDelete(delta, db, ledgerKey);
            innerResult().code(ManageKeyValueResultCode::SUCCESS);
            return true;
        }

        if (!keyValueFrame) {
            LedgerEntry mEntry;
            mEntry.data.type(LedgerEntryType::KEY_VALUE);
            mEntry.data.keyValue().key = mManageKeyValue.key;
            mEntry.data.keyValue().value = mManageKeyValue.action.value().value;
            keyValueHelper->storeAdd(delta, db, mEntry);
            innerResult().code(ManageKeyValueResultCode::SUCCESS);
            return true;
        }

        keyValueHelper->storeChange(delta, db, keyValueFrame->mEntry);
        innerResult().code(ManageKeyValueResultCode::SUCCESS);
        return true;

    }

    bool ManageKeyValueOpFrame::doCheckValid(Application &app) {
        return true;
    }

    std::unordered_map<AccountID, CounterpartyDetails>
    ManageKeyValueOpFrame::getCounterpartyDetails(Database &db, LedgerDelta *delta) const {
        return unordered_map<AccountID, CounterpartyDetails>();
    }

    SourceDetails ManageKeyValueOpFrame::getSourceAccountDetails(
            std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails, int32_t ledgerVersion) const
    {
        auto prefix = getPrefix();

        if(prefix.compare(kycRulesPrefix) == 0) {
            return SourceDetails({AccountType::MASTER}, static_cast<int32_t >(ThresholdIndexes::HIGH),
                                 static_cast<int32_t>(SignerType::KYC_SUPER_ADMIN));
        }

        return SourceDetails({AccountType::MASTER}, static_cast<int32_t >(ThresholdIndexes::HIGH),0);
    }

}
