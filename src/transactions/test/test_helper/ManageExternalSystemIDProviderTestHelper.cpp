#include "ManageExternalSystemIDProviderTestHelper.h"
#include "ledger/ExternalSystemAccountIDProviderHelper.h"
#include "transactions/ManageExternalSystemIDProviderOpFrame.h"
#include "test/test_marshaler.h"

namespace stellar
{

namespace txtest
{
    ManageExternalSystemIDProviderTestHelper::ManageExternalSystemIDProviderTestHelper(
            TestManager::pointer testManager) : TxHelper(testManager)
    {
    }

    TransactionFramePtr
    ManageExternalSystemIDProviderTestHelper::createManageExternalSystemIDProviderTx(Account &source,
                                                                                     ExternalSystemType type,
                                                                                     std::string data,
                                                                                     ManageExternalSystemIdProviderAction action,
                                                                                     Account *signer)
    {
        Operation op;
        op.body.type(OperationType::MANAGE_EXTERNAL_SYSTEM_ID_PROVIDER);
        auto& manageExSysIdProvider = op.body.manageExternalSystemIdProviderOp();
        manageExSysIdProvider.externalSystemType = type;
        manageExSysIdProvider.data = data;
        manageExSysIdProvider.action = action;

        return TxHelper::txFromOperation(source, op, signer);
    }

    ManageExternalSystemIdProviderResult
    ManageExternalSystemIDProviderTestHelper::applyManageExternalSystemIDProviderTx(Account &source,
                                                                                    ExternalSystemType type,
                                                                                    std::string data,
                                                                                    ManageExternalSystemIdProviderAction action,
                                                                                    ManageExternalSystemIdProviderResultCode expectedResultCode,
                                                                                    Account *signer)
    {
        TransactionFramePtr txFrame;

        auto providerHelper = ExternalSystemAccountIDProviderHelper::Instance();

        std::vector<ExternalSystemAccountIDProviderFrame::pointer> pool;
        Database& db = mTestManager->getDB();
        pool = providerHelper->loadPool(db);

        txFrame = createManageExternalSystemIDProviderTx(source, type, data, action, signer);

        mTestManager->applyCheck(txFrame);

        auto txResult = txFrame->getResult();
        auto actualResultCode =
                ManageExternalSystemProviderOpFrame::getInnerCode(txResult.result.results()[0]);

        REQUIRE(actualResultCode == expectedResultCode);

        auto txfee = mTestManager->getApp().getLedgerManager().getTxFee();
        REQUIRE(txResult.feeCharged == txfee);

        std::vector<ExternalSystemAccountIDProviderFrame::pointer> poolAfter;
        poolAfter = providerHelper->loadPool(db);

        auto opResult = txResult.result.results()[0].tr().manageExternalSystemIdProviderResult();

        if (actualResultCode != ManageExternalSystemIdProviderResultCode::SUCCESS)
        {
            REQUIRE(pool.size() == poolAfter.size());
        }
        else
        {
            if (action == ManageExternalSystemIdProviderAction::CREATE)
            {
                REQUIRE(pool.size() == poolAfter.size() - 1);
                auto provider = providerHelper->load(opResult.success().providerID, db);
                auto providerEntry = provider->getExternalSystemAccountIDProvider();
                REQUIRE(provider);
                REQUIRE(providerEntry.externalSystemType == type);
                REQUIRE(providerEntry.data == data);
                REQUIRE(providerEntry.accountID == nullptr);
                REQUIRE(providerEntry.expiresAt == NULL);
            }
            else
            {
                REQUIRE(pool.size() == poolAfter.size() + 1);
                REQUIRE(!providerHelper->load(opResult.success().providerID, db));
            }
        }

        return opResult;
    }
}
}
