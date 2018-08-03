
#ifndef STELLAR_MANAGEKEYVALUEOPFRAME_H
#define STELLAR_MANAGEKEYVALUEOPFRAME_H

#include <transactions/kyc/CreateKYCReviewableRequestOpFrame.h>
#include "transactions/OperationFrame.h"

namespace stellar {

    class ManageKeyValueOpFrame : public OperationFrame
    {
        ManageKeyValueOp const& mManageKeyValue;

        ManageKeyValueResult& innerResult()
        {
            return mResult.tr().manageKeyValueResult();
        }

    public:

        ManageKeyValueOpFrame(Operation const& op, OperationResult& res,
                              TransactionFrame& parentTx);

        bool doApply(Application& app, LedgerDelta& delta,
                     LedgerManager& ledgerManager) override;

        bool doCheckValid(Application& app) override;

        std::unordered_map<AccountID, CounterpartyDetails> getCounterpartyDetails(Database& db, LedgerDelta* delta) const override;

        SourceDetails getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                              int32_t ledgerVersion) const override;

        static ManageKeyValueResultCode
        getInnerCode(OperationResult const& res)
        {
            return res.tr().manageKeyValueResult().code();
        }

        std::string getInnerResultCodeAsStr() override
        {
            const auto result = getResult();
            const auto code = getInnerCode(result);
            return xdr::xdr_traits<ManageKeyValueResultCode>::enum_name(code);
        }

        string256 getPrefix() const {
            string256 prefix;
            std::istringstream from(mManageKeyValue.key);
            std::getline(from,prefix,':');

            return prefix;
        }

        static longstring makeKYCRuleKey(AccountType accountType, uint32 kycLevel, AccountType accountTypeToSet, uint32 kycLevelToSet);

        static longstring makeExternalSystemExpirationPeriodKey(int32 externalSystemType);

        static longstring makeMaxContractDetailsKey();

        static longstring makeMaxContractDetailLengthKey();

        static const char * kycRulesPrefix;
        static const char * externalSystemPrefix;
        static const char * maxContractDetailsPrefix;
        static const char * maxContractDetailLengthPrefix;
    };

}

#endif //STELLAR_MANAGEKEYVALUEOPFRAME_H
