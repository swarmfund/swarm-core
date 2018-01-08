#include "SetOptionsTestHelper.h"
#include "transactions/SetOptionsOpFrame.h"
#include "test/test_marshaler.h"

namespace stellar
{
namespace txtest
{
    SetOptionsTestHelper::SetOptionsTestHelper(TestManager::pointer testManager) : TxHelper(testManager)
    {

    }

    TransactionFramePtr
    SetOptionsTestHelper::createSetOptionsTx(Account &source, ThresholdSetter *thresholdSetter, Signer *signer,
                                             TrustData *trustData, LimitsUpdateRequestData *limitsUpdateRequestData)
    {
        Operation op;
        op.body.type(OperationType::SET_OPTIONS);

        SetOptionsOp& setOptionsOp = op.body.setOptionsOp();

        if (thresholdSetter)
        {
            if (thresholdSetter->masterWeight)
                setOptionsOp.masterWeight.activate() = *thresholdSetter->masterWeight;
            if (thresholdSetter->lowThreshold)
                setOptionsOp.lowThreshold.activate() = *thresholdSetter->lowThreshold;
            if (thresholdSetter->medThreshold)
                setOptionsOp.medThreshold.activate() = *thresholdSetter->medThreshold;
            if (thresholdSetter->highThreshold)
                setOptionsOp.highThreshold.activate() = *thresholdSetter->highThreshold;
        }

        if (signer)
            setOptionsOp.signer.activate() = *signer;

        if (trustData)
            setOptionsOp.trustData.activate() = *trustData;

        if (limitsUpdateRequestData)
            setOptionsOp.limitsUpdateRequestData.activate() = *limitsUpdateRequestData;

        return TxHelper::txFromOperation(source, op, nullptr);
    }

    void
    SetOptionsTestHelper::applySetOptionsTx(Account &source, ThresholdSetter *thresholdSetter, Signer *signer,
                                            TrustData *trustData, LimitsUpdateRequestData *limitsUpdateRequestData,
                                            SetOptionsResultCode expectedResult, SecretKey *txSigner)
    {
        TransactionFramePtr txFrame;

        txFrame = createSetOptionsTx(source, thresholdSetter, signer, trustData,
                                     limitsUpdateRequestData);
        if (txSigner)
        {
            txFrame->getEnvelope().signatures.clear();
            txFrame->addSignature(*txSigner);
        }

        mTestManager->applyCheck(txFrame);
        REQUIRE(SetOptionsOpFrame::getInnerCode(
                txFrame->getResult().result.results()[0]) == expectedResult);
    }
}
}