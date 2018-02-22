#include "BindExternalSystemAccountIdTestHelper.h"
#include "ledger/ExternalSystemAccountIDHelper.h"
#include "ledger/ExternalSystemAccountIDPoolEntryHelper.h"
#include "transactions/BindExternalSystemAccountIdOpFrame.h"
#include "test/test_marshaler.h"

namespace stellar
{

namespace txtest
{
    BindExternalSystemAccountIdTestHelper::BindExternalSystemAccountIdTestHelper(TestManager::pointer testManager)
            : TxHelper(testManager)
    {
    }

    TransactionFramePtr
    BindExternalSystemAccountIdTestHelper::createBindExternalSystemAccountIdTx(Account &source,
                                                                               ExternalSystemType externalSystemType,
                                                                               Account *signer)
    {
        Operation op;
        op.body.type(OperationType::BIND_EXTERNAL_SYSTEM_ACCOUNT_ID);
        auto& bindExternalSystemAccountId = op.body.bindExternalSystemAccountIdOp();
        bindExternalSystemAccountId.externalSystemType = externalSystemType;

        return TxHelper::txFromOperation(source, op, signer);
    }

    BindExternalSystemAccountIdResult
    BindExternalSystemAccountIdTestHelper::applyBindExternalSystemAccountIdTx(Account &source,
                                                                              ExternalSystemType externalSystemType,
                                                                              BindExternalSystemAccountIdResultCode expectedResultCode,
                                                                              Account *signer)
    {
        TransactionFramePtr txFrame;

        auto externalSystemAccountIDHelper = ExternalSystemAccountIDHelper::Instance();
        auto externalSystemAccountIDPoolEntryHelper = ExternalSystemAccountIDPoolEntryHelper::Instance();

        std::vector<ExternalSystemAccountIDFrame::pointer> externalSystemAccountIDs;
        Database& db = mTestManager->getDB();
        externalSystemAccountIDs = externalSystemAccountIDHelper->loadAll(db);

        txFrame = createBindExternalSystemAccountIdTx(source, externalSystemType, signer);

        mTestManager->applyCheck(txFrame);

        auto txResult = txFrame->getResult();
        auto actualResultCode = BindExternalSystemAccountIdOpFrame::getInnerCode(txResult.result.results()[0]);

        REQUIRE(actualResultCode == expectedResultCode);

        auto txFee = mTestManager->getApp().getLedgerManager().getTxFee();
        REQUIRE(txResult.feeCharged == txFee);

        std::vector<ExternalSystemAccountIDFrame::pointer> externalSystemAccountIDsAfter;
        externalSystemAccountIDsAfter = externalSystemAccountIDHelper->loadAll(db);

        auto opResult = txResult.result.results()[0].tr().bindExternalSystemAccountIdResult();

        if (actualResultCode != BindExternalSystemAccountIdResultCode::SUCCESS)
        {
            REQUIRE(externalSystemAccountIDs.size() == externalSystemAccountIDsAfter.size());
        }
        else
        {
            auto boundPoolEntryData = opResult.success().data;
            auto boundPoolEntryFrame = externalSystemAccountIDPoolEntryHelper->load(externalSystemType,
                                                                                    boundPoolEntryData, db);
            auto boundPoolEntry = boundPoolEntryFrame->getExternalSystemAccountIDPoolEntry();
            REQUIRE(boundPoolEntry.externalSystemType == externalSystemType);
            REQUIRE(boundPoolEntry.data == boundPoolEntryData);
            REQUIRE(*boundPoolEntry.accountID == source.key.getPublicKey());
            REQUIRE(boundPoolEntry.expiresAt == mTestManager->getLedgerManager().getCloseTime() + (24 * 60 * 60));

            REQUIRE(externalSystemAccountIDs.size() == externalSystemAccountIDsAfter.size() - 1);
            auto externalSystemAccountIDFrame = externalSystemAccountIDHelper->load(source.key.getPublicKey(),
                                                                               externalSystemType, db);
            auto externalSystemAccountID = externalSystemAccountIDFrame->getExternalSystemAccountID();
            REQUIRE(externalSystemAccountID.accountID == source.key.getPublicKey());
            REQUIRE(externalSystemAccountID.externalSystemType == externalSystemType);
            REQUIRE(externalSystemAccountID.data == boundPoolEntryData);
        }

        return opResult;
    }
}

}
