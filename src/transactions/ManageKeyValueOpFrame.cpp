
#include "ManageKeyValueOpFrame.h"
#include "ledger/LedgerDelta.h"
#include "ledger/KeyValueHelper.h"
#include "database/Database.h"
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"
#include <string>
#include <ledger/AccountHelper.h>
#include <transactions/kyc/CreateKYCReviewableRequestOpFrame.h>


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


    bool ManageKeyValueOpFrame::getKYCMask(Database &db, bool useKYCRules,
                                           CreateUpdateKYCRequestOpFrame *kycUpdateOpFrame, uint32 &allTasks)
    {
        if(!useKYCRules)
        {
            allTasks = !!kycUpdateOpFrame->getKYCUpdateOp().updateKYCRequestData.allTasks
                       ? kycUpdateOpFrame->getKYCUpdateOp().updateKYCRequestData.allTasks.activate()
                       : CreateUpdateKYCRequestOpFrame::defaultTasks;
            return true;
        }

        auto accountHelper = AccountHelper::Instance();
        auto account = accountHelper->loadAccount(kycUpdateOpFrame->getKYCUpdateOp().updateKYCRequestData.accountToUpdateKYC,db);

        //string256 key = "";
        string256 key = kycRulesPrefix + to_string(static_cast<uint32 >(account.get()->getAccount().accountType)) + ":" +
              to_string(static_cast<uint32 >(account.get()->getAccount().ext.kycLevel())) +":" +
              to_string(static_cast<uint32>(kycUpdateOpFrame->getOperation().body.createUpdateKYCRequestOp().updateKYCRequestData.accountTypeToSet)) + ":" +
              to_string(static_cast<uint32>(kycUpdateOpFrame->getOperation().body.createUpdateKYCRequestOp().updateKYCRequestData.kycLevelToSet));


        auto kvEntry = KeyValueHelper::Instance()->loadKeyValue(key,db);

        if (kvEntry == nullptr)
        {
            return false;
        }

        allTasks = kvEntry.get()->getKeyValue().value.defaultMask();
        /*
        requestEntry.body.updateKYCRequest().allTasks = !!mCreateUpdateKYCRequest.updateKYCRequestData.allTasks
                                                        ? mCreateUpdateKYCRequest.updateKYCRequestData.allTasks.activate()
                                                        : CreateUpdateKYCRequestOpFrame::defaultTasks;
        */
        return true;
    }

}
