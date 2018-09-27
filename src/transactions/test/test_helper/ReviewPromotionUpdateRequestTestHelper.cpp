#include <ledger/ReviewableRequestHelper.h>
#include <ledger/AssetHelperLegacy.h>
#include <ledger/SaleHelper.h>
#include <transactions/review_request/ReviewSaleCreationRequestOpFrame.h>
#include <ledger/AssetPairHelper.h>
#include "test/test_marshaler.h"

#include "ReviewPromotionUpdateRequestTestHelper.h"

namespace stellar {
    namespace txtest {
        PromotionUpdateReviewChecker::PromotionUpdateReviewChecker(TestManager::pointer testManager,
                                                                   uint64_t requestID) : ReviewChecker(testManager) {
            auto &db = mTestManager->getDB();

            auto request = ReviewableRequestHelper::Instance()->loadRequest(requestID, db);
            if (!request || request->getType() != ReviewableRequestType::UPDATE_PROMOTION) {
                return;
            }

            promotionUpdateRequest = std::make_shared<PromotionUpdateRequest>(
                    request->getRequestEntry().body.promotionUpdateRequest());
            baseAssetBeforeTx = AssetHelperLegacy::Instance()->loadAsset(promotionUpdateRequest->newPromotionData.baseAsset,
                                                                   db);
            saleBeforeTx = SaleHelper::Instance()->loadSale(promotionUpdateRequest->promotionID, db);
        }

        void PromotionUpdateReviewChecker::checkApprove(ReviewableRequestFrame::pointer) {
            auto &db = mTestManager->getDB();

            REQUIRE(!!promotionUpdateRequest);
            REQUIRE(!!baseAssetBeforeTx);
            REQUIRE(!!saleBeforeTx);

            auto baseAssetAfterTx = AssetHelperLegacy::Instance()->loadAsset(
                    promotionUpdateRequest->newPromotionData.baseAsset, db);
            REQUIRE(!!baseAssetAfterTx);

            auto saleAfterTx = SaleHelper::Instance()->loadSale(promotionUpdateRequest->promotionID, db);
            REQUIRE(!!saleAfterTx);

            auto newPromotionData = promotionUpdateRequest->newPromotionData;

            uint64_t hardCapInBaseAsset = newPromotionData.ext.v() >=
                                          LedgerVersion::ALLOW_TO_SPECIFY_REQUIRED_BASE_ASSET_AMOUNT_FOR_HARD_CAP
                                          ? ReviewSaleCreationRequestOpFrame::getRequiredBaseAssetForHardCap(
                            newPromotionData)
                                          : baseAssetBeforeTx->getMaxIssuanceAmount();

            REQUIRE(baseAssetBeforeTx->getPendingIssuance() + hardCapInBaseAsset -
                    saleBeforeTx->getMaxAmountToBeSold() == baseAssetAfterTx->getPendingIssuance());

            REQUIRE(saleBeforeTx->getID() == saleAfterTx->getID());

            // check if asset pair was created
            for (auto saleQuoteAsset : newPromotionData.quoteAssets) {
                auto assetPair = AssetPairHelper::Instance()->loadAssetPair(newPromotionData.baseAsset,
                                                                            saleQuoteAsset.quoteAsset,
                                                                            mTestManager->getDB());
                REQUIRE(!!assetPair);
                REQUIRE(assetPair->getCurrentPrice() == saleQuoteAsset.price);
            }
        }

        ReviewPromotionUpdateRequestHelper::ReviewPromotionUpdateRequestHelper(TestManager::pointer testManager)
                : ReviewRequestHelper(testManager) {
        }

        ReviewRequestResult
        ReviewPromotionUpdateRequestHelper::applyReviewRequestTx(Account &source, uint64_t requestID, Hash requestHash,
                                                                 ReviewableRequestType requestType,
                                                                 ReviewRequestOpAction action, std::string rejectReason,
                                                                 ReviewRequestResultCode expectedResult) {
            auto checker = PromotionUpdateReviewChecker(mTestManager, requestID);
            return ReviewRequestHelper::applyReviewRequestTx(source, requestID, requestHash, requestType, action,
                                                             rejectReason, expectedResult, checker);
        }
    }
}