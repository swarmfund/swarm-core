#pragma "once"

#include "transactions/OperationFrame.h"

namespace stellar {
    class ManagePolicyAttachmentOpFrame : public OperationFrame {
        static const uint32 sPolicyAttachmentsLimit;

        ManagePolicyAttachmentResult &innerResult() {
            return mResult.tr().managePolicyAttachmentResult();
        }

        ManagePolicyAttachmentOp const &mManagePolicyAttachment;

        std::unordered_map<AccountID, CounterpartyDetails>
        getCounterpartyDetails(Database &db, LedgerDelta *delta) const override;

        SourceDetails
        getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                int32_t ledgerVersion) const override;

    public:
        ManagePolicyAttachmentOpFrame(Operation const &op, OperationResult &res, TransactionFrame &parentTx);

        bool deletePolicyAttachment(Application &app, LedgerDelta &delta, Database &db);

        bool doApply(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager) override;

        bool doCheckValid(Application &app) override;

        static ManagePolicyAttachmentResultCode getInnerCode(OperationResult const &res) {
            return res.tr().managePolicyAttachmentResult().code();
        }

        std::string getInnerResultCodeAsStr() override {
            return xdr::xdr_traits<ManagePolicyAttachmentResultCode>::enum_name(innerResult().code());
        }

        uint32_t getPolicyAttachmentsLimit(Database &db, int32_t ledgerVersion) const {
            return sPolicyAttachmentsLimit;
        }
    };
}