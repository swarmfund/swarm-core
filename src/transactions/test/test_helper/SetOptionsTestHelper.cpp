//
// Created by volodymyr on 04.01.18.
//

#include <transactions/SetOptionsOpFrame.h>
#include <transactions/test/TxTests.h>
#include <ledger/AccountHelper.h>
#include <ledger/ReviewableRequestHelper.h>
#include "SetOptionsTestHelper.h"
#include "test/test_marshaler.h"

using xdr::operator==;

namespace stellar
{

namespace txtest
{
    SetOptionsTestHelper::SetOptionsTestHelper(TestManager::pointer testManager) : TxHelper(testManager)
    {
    }

    TransactionFramePtr
    SetOptionsTestHelper::createSetOptionsTx(Account &source, ThresholdSetter *thresholds, Signer *signer, TrustData *trustData,
                                                 UpdateKYCData *updateKYCData)
    {
        Operation op;
        op.body.type(OperationType::SET_OPTIONS);
        auto& setOptions = op.body.setOptionsOp();

        if (thresholds)
        {
            if (thresholds->masterWeight)
            {
                setOptions.masterWeight.activate() = *thresholds->masterWeight;
            }
            if (thresholds->lowThreshold)
            {
                setOptions.lowThreshold.activate() = *thresholds->lowThreshold;
            }
            if (thresholds->medThreshold)
            {
                setOptions.medThreshold.activate() = *thresholds->medThreshold;
            }
            if (thresholds->highThreshold)
            {
                setOptions.highThreshold.activate() = *thresholds->highThreshold;
            }
        }

        if (signer)
            setOptions.signer.activate() = *signer;

        if (trustData)
            setOptions.trustData.activate() = *trustData;

        if (updateKYCData)
            setOptions.updateKYCData.activate() = *updateKYCData;

        return txFromOperation(source, op);
    }

    SetOptionsResult
    SetOptionsTestHelper::applySetOptionsTx(Account &source, ThresholdSetter *thresholds, Signer *signer,
                                            TrustData *trustData, UpdateKYCData* updateKYCData,
                                            SetOptionsResultCode expectedResult)
    {
        TransactionFramePtr txFrame = createSetOptionsTx(source, thresholds, signer, trustData, updateKYCData);

        mTestManager->applyCheck(txFrame);

        auto opResult = txFrame->getResult().result.results()[0];

        REQUIRE(opResult.code() == OperationResultCode::opINNER);

        auto actualResultCode = SetOptionsOpFrame::getInnerCode(opResult);
        REQUIRE(actualResultCode == expectedResult);

        auto setOptionsResult = opResult.tr().setOptionsResult();
        if (actualResultCode != SetOptionsResultCode::SUCCESS)
            return setOptionsResult;

        auto accountAfterTx = AccountHelper::Instance()->mustLoadAccount(source.key.getPublicKey(), mTestManager->getDB());

        if (thresholds)
            checkUpdateThresholds(thresholds, accountAfterTx);

        if (signer)
            REQUIRE(checkUpdateSigner(signer, accountAfterTx));

        return setOptionsResult;
    }

    void SetOptionsTestHelper::checkUpdateThresholds(ThresholdSetter *thresholds, AccountFrame::pointer accountAfterTx)
    {
        if (thresholds->masterWeight)
            REQUIRE(*thresholds->masterWeight == accountAfterTx->getMasterWeight());

        if (thresholds->lowThreshold)
            REQUIRE(*thresholds->lowThreshold == accountAfterTx->getLowThreshold());

        if (thresholds->medThreshold)
            REQUIRE(*thresholds->medThreshold == accountAfterTx->getMediumThreshold());

        if (thresholds->highThreshold)
            REQUIRE(*thresholds->highThreshold == accountAfterTx->getHighThreshold());
    }

    bool SetOptionsTestHelper::checkUpdateSigner(Signer *expectedSigner, AccountFrame::pointer accountAfterTx)
    {
        auto signers = accountAfterTx->getAccount().signers;
        for (auto signer : signers)
        {
            if (signer.pubKey == expectedSigner->pubKey)
            {
                REQUIRE(signer == *expectedSigner);
                return true;
            }
        }

        //if weight is zero than signer have just been removed
        return expectedSigner->weight == 0;
    }

}

}



