#pragma once

#include "transactions/OperationFrame.h"

#include "ledger/ReviewableRequestHelper.h"

namespace stellar {
    class CreateUpdateKYCRequestOpFrame : public OperationFrame {
        CreateUpdateKYCRequestOp mCreateUpdateKYCRequest;

        std::unordered_map<AccountID, CounterpartyDetails>
        getCounterpartyDetails(Database &db, LedgerDelta *delta) const override;

        SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                              int32_t ledgerVersion) const override;

        CreateUpdateKYCRequestResult &
        innerResult() {
            return mResult.tr().createUpdateKYCRequestResult();
        }

        std::string getReference() const;

        void createRequest(ReviewableRequestEntry &requestEntry, uint32 defaultMask);

        void updateRequest(ReviewableRequestEntry &requestEntry);

        bool changeUpdateKYCRequest(Database &db, LedgerDelta &delta, Application &app);

        void tryAutoApprove(Database &db, LedgerDelta &delta, Application &app,
                            ReviewableRequestFrame::pointer requestFrame);

        bool ensureUpdateKYCDataValid(ReviewableRequestEntry& requestEntry);

    public:
        CreateUpdateKYCRequestOpFrame(Operation const &op, OperationResult &res, TransactionFrame &parentTx);

        bool doApply(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager) override;

        bool doCheckValid(Application &app) override;

        static bool getDefaultKYCMask(Database &db, LedgerManager &ledgerManager, UpdateKYCRequestData kycRequestData,
                               AccountFrame::pointer account, uint32 &defaultMask);

        static void checkRequestType(ReviewableRequestFrame::pointer request);

        static CreateUpdateKYCRequestResultCode getInnerCode(OperationResult const &res) {
            return res.tr().createUpdateKYCRequestResult().code();
        }

        std::string getInnerResultCodeAsStr() override {
            return xdr::xdr_traits<CreateUpdateKYCRequestResultCode>::enum_name(innerResult().code());
        }

        static const uint32 defaultTasks;
    };
}