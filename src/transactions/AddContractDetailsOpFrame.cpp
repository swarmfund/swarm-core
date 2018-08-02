// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <ledger/ContractHelper.h>
#include "transactions/review_request/ReviewRequestHelper.h"
#include "transactions/AddContractDetailsOpFrame.h"
#include "ledger/TrustFrame.h"
#include "ledger/TrustHelper.h"
#include "ledger/BalanceHelper.h"
#include "ledger/ReviewableRequestHelper.h"
#include "crypto/SHA.h"
#include "database/Database.h"
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"

namespace stellar
{
using xdr::operator==;

std::unordered_map<AccountID, CounterpartyDetails>
AddContractDetailsOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
{
    // no counterparties
    return{};
}

SourceDetails
AddContractDetailsOpFrame::getSourceAccountDetails(
        std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                                         int32_t ledgerVersion) const
{
    std::vector<AccountType> allowedAccountTypes = {AccountType::MASTER, AccountType::GENERAL,
                                                    AccountType::NOT_VERIFIED,
                               AccountType::SYNDICATE, AccountType::EXCHANGE, AccountType::VERIFIED,
                               AccountType::ACCREDITED_INVESTOR, AccountType::INSTITUTIONAL_INVESTOR};

    return SourceDetails(allowedAccountTypes,
                         mSourceAccount->getHighThreshold(),
                         static_cast<int32_t>(SignerType::ACCOUNT_MANAGER),
                         static_cast<int32_t>(BlockReasons::KYC_UPDATE) |
                         static_cast<int32_t>(BlockReasons::TOO_MANY_KYC_UPDATE_REQUESTS));
}

AddContractDetailsOpFrame::AddContractDetailsOpFrame(Operation const& op, OperationResult& res,
        TransactionFrame& parentTx)
        : OperationFrame(op, res, parentTx)
        , mAddContractDetails(mOperation.body.addContractDetailsOp())
{
}

std::string
AddContractDetailsOpFrame::getInnerResultCodeAsStr()
{
    const auto result = getResult();
    const auto code = getInnerCode(result);
    return xdr::xdr_traits<AddContractDetailsResultCode>::enum_name(code);
}

bool
AddContractDetailsOpFrame::doApply(Application& app, LedgerDelta& delta,
                                   LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();
    AccountEntry& account = mSourceAccount->getAccount();

    innerResult().code(AddContractDetailsResultCode::SUCCESS);

    auto contractHelper = ContractHelper::Instance();
    auto contractFrame = contractHelper->loadContract(mAddContractDetails.contractID, db, &delta);

    if (!contractFrame)
    {
        innerResult().code(AddContractDetailsResultCode::NOT_FOUND);
        return false;
    }

    if (!(contractFrame->getContractor() == getSourceID()) &&
        !(contractFrame->getCustomer() == getSourceID()))
    {
        innerResult().code(AddContractDetailsResultCode::NOT_ALLOWED);
        return false;
    }

    contractFrame->addContractDetails(mAddContractDetails.details);
    contractHelper->storeChange(delta, db, contractFrame->mEntry);

    return true;
}

bool
AddContractDetailsOpFrame::doCheckValid(Application& app)
{
    if (mAddContractDetails.details.empty())
    {
        innerResult().code(AddContractDetailsResultCode::MALFORMED);
        return false;
    }

    return true;
}

}
