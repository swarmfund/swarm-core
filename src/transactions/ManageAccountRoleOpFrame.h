#pragma "once"

#include "transactions/OperationFrame.h"

namespace stellar
{
class ManageAccountRoleOpFrame : public OperationFrame
{
  public:
    ManageAccountRoleOpFrame(Operation const& op, OperationResult& res,
                             TransactionFrame& parentTx);

    bool doApply(Application& app, StorageHelper& storageHelper,
                 LedgerManager& ledgerManager) override;

    bool doCheckValid(Application& app) override;

    static ManageAccountRoleResultCode
    getInnerCode(OperationResult const& res)
    {
        return res.tr().manageAccountRoleResult().code();
    }

    std::string
    getInnerResultCodeAsStr() override
    {
        return xdr::xdr_traits<ManageAccountRoleResultCode>::enum_name(
            innerResult().code());
    }

  private:
    ManageAccountRoleResult&
    innerResult()
    {
        return mResult.tr().manageAccountRoleResult();
    }

    ManageAccountRoleOp const& mManageAccountRole;

    std::unordered_map<AccountID, CounterpartyDetails>
    getCounterpartyDetails(Database& db, LedgerDelta* delta) const override;

    SourceDetails
    getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails>
                                counterpartiesDetails,
                            int32_t ledgerVersion) const override;

    bool createAccountRole(Application& app, StorageHelper& storageHelper);
    bool deleteAccountRole(Application& app, StorageHelper& storageHelper);
};
} // namespace stellar