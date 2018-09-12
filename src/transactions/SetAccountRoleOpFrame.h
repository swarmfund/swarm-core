#pragma "once"

#include "transactions/OperationFrame.h"

namespace stellar
{
class SetAccountRoleOpFrame : public OperationFrame
{
    SetAccountRoleResult&
    innerResult()
    {
        return mResult.tr().setAccountRoleResult();
    }

    SetAccountRoleOp const& mSetAccountRole;

    std::unordered_map<AccountID, CounterpartyDetails>
    getCounterpartyDetails(Database& db, LedgerDelta* delta) const override;

    SourceDetails
    getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails>
                                counterpartiesDetails,
                            int32_t ledgerVersion) const override;

  public:
    SetAccountRoleOpFrame(Operation const& op, OperationResult& res,
                          TransactionFrame& parentTx);

    bool doApply(Application& app, StorageHelper& storageHelper,
                 LedgerManager& ledgerManager) override;

    bool doCheckValid(Application& app) override;

    static SetAccountRoleResultCode
    getInnerCode(OperationResult const& res)
    {
        return res.tr().setAccountRoleResult().code();
    }

    std::string
    getInnerResultCodeAsStr() override
    {
        return xdr::xdr_traits<SetAccountRoleResultCode>::enum_name(
            innerResult().code());
    }

  private:
    bool createAccountRole(Application& app, StorageHelper& storageHelper);
    bool deleteAccountRole(Application& app, StorageHelper& storageHelper);
};
} // namespace stellar