#include "BindExternalSystemAccountIdTestHelper.h"
#include "ledger/ExternalSystemAccountIDHelperLegacy.h"
#include "ledger/ExternalSystemAccountIDPoolEntryHelperLegacy.h"
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
                                                                               int32 externalSystemType,
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
                                                                              int32 externalSystemType,
                                                                              BindExternalSystemAccountIdResultCode expectedResultCode,
                                                                              Account *signer)
    {
        TransactionFramePtr txFrame;

        auto externalSystemAccountIDHelper = ExternalSystemAccountIDHelperLegacy::Instance();
        auto externalSystemAccountIDPoolEntryHelper = ExternalSystemAccountIDPoolEntryHelperLegacy::Instance();

        std::vector<ExternalSystemAccountIDFrame::pointer> externalSystemAccountIDs;
        Database& db = mTestManager->getDB();
        externalSystemAccountIDs = externalSystemAccountIDHelper->loadAll(db);
        auto poolEntryToBindFrame = externalSystemAccountIDPoolEntryHelper->loadAvailablePoolEntry(db, mTestManager->getLedgerManager(),
                                                                                                   externalSystemType);
        bool rebinding = false;
        ExternalSystemAccountIDFrame::pointer externalSystemAccountIDBeforeTx;
        bool prolongation = externalSystemAccountIDHelper->exists(db, source.key.getPublicKey(), externalSystemType);
        if (!prolongation)
        {
            if (!!poolEntryToBindFrame && !!poolEntryToBindFrame->getExternalSystemAccountIDPoolEntry().accountID)
            {
                auto poolEntryToBind = poolEntryToBindFrame->getExternalSystemAccountIDPoolEntry();
                externalSystemAccountIDBeforeTx = externalSystemAccountIDHelper->load(*poolEntryToBind.accountID,
                                                                                      externalSystemType, db);
                if (!!externalSystemAccountIDBeforeTx)
                    rebinding = true;
            }
        }

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
            if (prolongation || rebinding)
                REQUIRE(externalSystemAccountIDs.size() == externalSystemAccountIDsAfter.size());
            else
                REQUIRE(externalSystemAccountIDs.size() == externalSystemAccountIDsAfter.size() - 1);

            auto boundPoolEntryData = opResult.success().data;
            auto boundPoolEntryFrame = externalSystemAccountIDPoolEntryHelper->load(externalSystemType,
                                                                                    boundPoolEntryData, db);
            REQUIRE(!!boundPoolEntryFrame);

            auto boundPoolEntry = boundPoolEntryFrame->getExternalSystemAccountIDPoolEntry();
            REQUIRE(boundPoolEntry.externalSystemType == externalSystemType);
            REQUIRE(boundPoolEntry.data == boundPoolEntryData);
            REQUIRE(!!boundPoolEntry.accountID);
            REQUIRE(*boundPoolEntry.accountID == source.key.getPublicKey());
            REQUIRE(boundPoolEntry.expiresAt == mTestManager->getLedgerManager().getCloseTime() + BindExternalSystemAccountIdOpFrame::dayInSeconds);

            if (rebinding)
                REQUIRE(!externalSystemAccountIDHelper->exists(db, externalSystemAccountIDBeforeTx->getExternalSystemAccountID().accountID,
                                                               externalSystemType));

            auto externalSystemAccountIDFrame = externalSystemAccountIDHelper->load(source.key.getPublicKey(),
                                                                               externalSystemType, db);
            REQUIRE(!!externalSystemAccountIDFrame);

            auto externalSystemAccountID = externalSystemAccountIDFrame->getExternalSystemAccountID();
            REQUIRE(externalSystemAccountID.accountID == source.key.getPublicKey());
            REQUIRE(externalSystemAccountID.externalSystemType == externalSystemType);
            REQUIRE(externalSystemAccountID.data == boundPoolEntryData);
        }

        return opResult;
    }
}

}
