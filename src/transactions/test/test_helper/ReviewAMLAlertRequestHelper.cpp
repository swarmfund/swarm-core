#include "ReviewAMLAlertRequestHelper.h"
#include "ledger/AssetFrame.h"
#include "ledger/AssetHelperLegacy.h"
#include "test/test_marshaler.h"
#include "ledger/BalanceHelperLegacy.h"


namespace stellar
{
namespace txtest
{
void AmlReviewChecker::checkApproval(AMLAlertRequest const& request,
    AccountID const& requestor)
{
    auto balanceBeforeTx = mStateBeforeTxHelper.getBalance(request.balanceID);
    REQUIRE(!!balanceBeforeTx);
    auto balanceAfterTx = BalanceHelperLegacy::Instance()->loadBalance(request.balanceID, mTestManager->getDB());
    REQUIRE(!!balanceAfterTx);
    REQUIRE(balanceBeforeTx->getAmount() == balanceAfterTx->getAmount());
    REQUIRE(balanceBeforeTx->getLocked() - request.amount == balanceAfterTx->getLocked());

    auto assetBeforeTx = mStateBeforeTxHelper.getAssetEntry(balanceBeforeTx->getAsset());
    auto assetFrameAfterTx = AssetHelperLegacy::Instance()->loadAsset(balanceBeforeTx->getAsset(), mTestManager->getDB());
    auto assetAfterTx = assetFrameAfterTx->getAsset();
    REQUIRE(assetBeforeTx.availableForIssueance == assetAfterTx.availableForIssueance);
    REQUIRE(assetBeforeTx.maxIssuanceAmount == assetAfterTx.maxIssuanceAmount);
    REQUIRE(assetBeforeTx.issued - request.amount == assetAfterTx.issued);
}

void AmlReviewChecker::checkPermanentReject(AMLAlertRequest const& request,
    AccountID const& requestor)
{
    auto balanceBeforeTx = mStateBeforeTxHelper.getBalance(request.balanceID);
    REQUIRE(!!balanceBeforeTx);
    auto balanceAfterTx = BalanceHelperLegacy::Instance()->loadBalance(request.balanceID, mTestManager->getDB());
    REQUIRE(!!balanceAfterTx);
    REQUIRE(balanceBeforeTx->getAmount() + request.amount == balanceAfterTx->getAmount());
    REQUIRE(balanceBeforeTx->getLocked() - request.amount == balanceAfterTx->getLocked());

    auto assetBeforeTx = mStateBeforeTxHelper.getAssetFrame(balanceBeforeTx->getAsset());
    // asset has not been changed, so it should not be available in mStateBeforeTxHelper
    REQUIRE(!assetBeforeTx);
}

void AmlReviewChecker::checkApprove(ReviewableRequestFrame::pointer request)
{
    if (request->getType() != ReviewableRequestType::AML_ALERT)
    {
        throw std::runtime_error("Expected aml alert request type");
    }

    return checkApproval(request->getRequestEntry().body.amlAlertRequest(), request->getRequestor());
}

void AmlReviewChecker::checkPermanentReject(ReviewableRequestFrame::pointer request)
{
    if (request->getType() != ReviewableRequestType::AML_ALERT)
    {
        throw std::runtime_error("Expected aml alert request type");
    }

    return checkPermanentReject(request->getRequestEntry().body.amlAlertRequest(), request->getRequestor());
}

ReviewAmlAlertHelper::ReviewAmlAlertHelper(TestManager::pointer testManager) : ReviewRequestHelper(testManager)
{
}

ReviewRequestResult ReviewAmlAlertHelper::applyReviewRequestTx(Account& source,
    uint64_t requestID, Hash requestHash, ReviewableRequestType requestType,
    ReviewRequestOpAction action, std::string rejectReason,
    ReviewRequestResultCode expectedResult)
{
    auto amlReviewChecker = AmlReviewChecker(mTestManager);
    return ReviewRequestHelper::applyReviewRequestTx(source, requestID,
        requestHash, requestType,
        action, rejectReason,
        expectedResult,
        amlReviewChecker);
}
}
}
