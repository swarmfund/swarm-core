#include "ManageExternalSystemAccountIDPoolEntryTestHelper.h"
#include "ledger/ExternalSystemAccountIDPoolEntryHelper.h"
#include "transactions/manage_external_system_account_id_pool/ManageExternalSystemAccountIDPoolEntryOpFrame.h"
#include "test/test_marshaler.h"

namespace stellar
{

namespace txtest
{
    ManageExternalSystemAccountIDPoolEntryTestHelper::ManageExternalSystemAccountIDPoolEntryTestHelper(
            TestManager::pointer testManager) : TxHelper(testManager)
    {
    }

    TransactionFramePtr
    ManageExternalSystemAccountIDPoolEntryTestHelper::createManageExternalSystemAccountIDPoolEntryTx(Account &source,
                                                ManageExternalSystemAccountIdPoolEntryOp::_actionInput_t actionInput,
                                                ManageExternalSystemAccountIdPoolEntryAction action,
                                                Account *signer)
    {
        Operation op;
        op.body.type(OperationType::MANAGE_EXTERNAL_SYSTEM_ACCOUNT_ID_POOL_ENTRY);
        auto& manageExSysAccIdPoolEntry = op.body.manageExternalSystemAccountIdPoolEntryOp();
        manageExSysAccIdPoolEntry.actionInput = actionInput;

        return TxHelper::txFromOperation(source, op, signer);
    }

    ManageExternalSystemAccountIdPoolEntryResult
    ManageExternalSystemAccountIDPoolEntryTestHelper::applyManageExternalSystemAccountIDPoolEntryTx(Account &source,
                                                ManageExternalSystemAccountIdPoolEntryOp::_actionInput_t actionInput,
                                                ManageExternalSystemAccountIdPoolEntryAction action,
                                                ManageExternalSystemAccountIdPoolEntryResultCode expectedResultCode,
                                                Account *signer)
    {
        TransactionFramePtr txFrame;

        auto poolEntryHelper = ExternalSystemAccountIDPoolEntryHelper::Instance();

        std::vector<ExternalSystemAccountIDPoolEntryFrame::pointer> pool;
        Database& db = mTestManager->getDB();
        pool = poolEntryHelper->loadPool(db);

        txFrame = createManageExternalSystemAccountIDPoolEntryTx(source, actionInput, action, signer);

        mTestManager->applyCheck(txFrame);

        auto txResult = txFrame->getResult();
        auto actualResultCode =
                ManageExternalSystemAccountIdPoolEntryOpFrame::getInnerCode(txResult.result.results()[0]);

        REQUIRE(actualResultCode == expectedResultCode);

        auto txfee = mTestManager->getApp().getLedgerManager().getTxFee();
        REQUIRE(txResult.feeCharged == txfee);

        std::vector<ExternalSystemAccountIDPoolEntryFrame::pointer> poolAfter;
        poolAfter = poolEntryHelper->loadPool(db);

        auto opResult = txResult.result.results()[0].tr().manageExternalSystemAccountIdPoolEntryResult();

        if (actualResultCode != ManageExternalSystemAccountIdPoolEntryResultCode::SUCCESS)
        {
            return opResult;
        }

        if (action == ManageExternalSystemAccountIdPoolEntryAction::CREATE)
        {
            REQUIRE(pool.size() == poolAfter.size() - 1);

            auto poolEntryFrame = poolEntryHelper->load(opResult.success().poolEntryID, db);
            REQUIRE(poolEntryFrame);

            auto poolEntry = poolEntryFrame->getExternalSystemAccountIDPoolEntry();
            REQUIRE(poolEntry.externalSystemType ==
                            actionInput.createExternalSystemAccountIdPoolEntryActionInput().externalSystemType);
            REQUIRE(poolEntry.data ==
                            actionInput.createExternalSystemAccountIdPoolEntryActionInput().data);
            REQUIRE(poolEntry.accountID == nullptr);
            REQUIRE(poolEntry.expiresAt == 0);
        }
        else
        {
            throw std::runtime_error("Unexpected action on manage external system account id pool entry operation");
        }

        return opResult;
    }

    ManageExternalSystemAccountIdPoolEntryResult
    ManageExternalSystemAccountIDPoolEntryTestHelper::createExternalSystemAccountIdPoolEntry(Account &source,
                                                 ExternalSystemType type, std::string data,
                                                 ManageExternalSystemAccountIdPoolEntryResultCode expectedResultCode,
                                                 Account *signer)
    {
        ManageExternalSystemAccountIdPoolEntryOp::_actionInput_t actionInput;
        actionInput.action(ManageExternalSystemAccountIdPoolEntryAction::CREATE);
        CreateExternalSystemAccountIdPoolEntryActionInput& input =
                actionInput.createExternalSystemAccountIdPoolEntryActionInput();
        input.externalSystemType = type;
        input.data = data;
        return applyManageExternalSystemAccountIDPoolEntryTx(source, actionInput,
                                                             ManageExternalSystemAccountIdPoolEntryAction::CREATE,
                                                             expectedResultCode, signer);
    }
}
}
