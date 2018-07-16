#pragma once

#include "main/Application.h"
#include "medida/metrics_registry.h"
#include "xdr/Stellar-operation-manage-sale.h"
#include "xdrpp/printer.h"
#include <crypto/SHA.h>
#include <ledger/ReviewableRequestFrame.h>
#include <ledger/SaleFrame.h>
#include <lib/xdrpp/xdrpp/marshal.h>
#include <transactions/OperationFrame.h>

namespace stellar {
    class ManageSaleOpFrame : public OperationFrame {
    protected:
        ManageSaleOp const &mManageSaleOp;

        ManageSaleResult &innerResult() {
            return mResult.tr().manageSaleResult();
        }

        std::unordered_map<AccountID, CounterpartyDetails>
        getCounterpartyDetails(Database &db, LedgerDelta *delta) const override;

        SourceDetails
        getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                int32_t ledgerVersion) const override;

        void trySetFulfilled(LedgerManager &lm, bool fulfilled);

    public:
        ManageSaleOpFrame(Operation const &op, OperationResult &opRes, TransactionFrame &parentTx);

        bool createUpdateSaleDetailsRequest(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager,
                                            Database &db);

        bool amendUpdateSaleDetailsRequest(LedgerManager &lm, Database &db, LedgerDelta &delta);

        bool createUpdateEndTimeRequest(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager,
                                        Database &db);

        bool amendUpdateEndTimeRequest(LedgerManager &lm, Database &db, LedgerDelta &delta);

        bool setSaleState(SaleFrame::pointer sale, Application &app, LedgerDelta &delta, LedgerManager &ledgerManager,
                          Database &db);

        bool isPromotionUpdateDataValid(Application &app);

        bool createPromotionUpdateRequest(Application &app, LedgerDelta &delta, Database &db, SaleState saleState);

        void tryAutoApprove(Application &app, Database &db, LedgerDelta &delta,
                            ReviewableRequestFrame::pointer requestFrame);

        bool amendPromotionUpdateRequest(LedgerManager &lm, Database &db, LedgerDelta &delta);

        bool doCheckValid(Application &app) override;

        bool doApply(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager) override;

        static void checkRequestType(ReviewableRequestFrame::pointer request, ReviewableRequestType requestType);

        static void cancelSale(SaleFrame::pointer sale, LedgerDelta &delta, Database &db, LedgerManager &lm);

        static void
        cancelAllOffersForQuoteAsset(SaleFrame::pointer sale, SaleQuoteAsset const &saleQuoteAsset,
                                     LedgerDelta &delta, Database &db);

        static void deleteAllAntesForSale(uint64_t saleID, LedgerDelta &delta, Database &db);

        static bool isSaleStateValid(LedgerManager &lm, SaleState saleState);

        std::string getUpdateSaleDetailsRequestReference() const {
            const auto hash = sha256(xdr::xdr_to_opaque(ReviewableRequestType::UPDATE_SALE_DETAILS,
                                                        mManageSaleOp.saleID));
            return binToHex(hash);
        }

        std::string getUpdateSaleEndTimeRequestReference() const {
            const auto hash = sha256(xdr::xdr_to_opaque(ReviewableRequestType::UPDATE_SALE_END_TIME,
                                                        mManageSaleOp.saleID));
            return binToHex(hash);
        }

        std::string getPromotionUpdateRequestReference() const {
            const auto hash = sha256(xdr::xdr_to_opaque(ReviewableRequestType::UPDATE_PROMOTION, mManageSaleOp.saleID));
            return binToHex(hash);
        }

        static ManageSaleResultCode
        getInnerCode(OperationResult &res) {
            return res.tr().manageSaleResult().code();
        }

        std::string getInnerResultCodeAsStr() override {
            return xdr::xdr_traits<ManageSaleResultCode>::enum_name(innerResult().code());
        }
    };
}
