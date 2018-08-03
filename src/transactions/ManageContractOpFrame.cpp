// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <ledger/ContractHelper.h>
#include <ledger/KeyValueHelper.h>
#include "transactions/review_request/ReviewRequestHelper.h"
#include "transactions/ManageContractOpFrame.h"
#include "ledger/TrustFrame.h"
#include "ledger/TrustHelper.h"
#include "ledger/BalanceHelper.h"
#include "ledger/ReviewableRequestHelper.h"
#include "crypto/SHA.h"
#include "database/Database.h"
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"
#include "ManageKeyValueOpFrame.h"

namespace stellar
{
using xdr::operator==;

std::unordered_map<AccountID, CounterpartyDetails>
ManageContractOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
{
    // no counterparties
    return{};
}

SourceDetails
ManageContractOpFrame::getSourceAccountDetails(
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

ManageContractOpFrame::ManageContractOpFrame(Operation const& op, OperationResult& res,
        TransactionFrame& parentTx)
        : OperationFrame(op, res, parentTx)
        , mManageContract(mOperation.body.manageContractOp())
{
}

std::string
ManageContractOpFrame::getInnerResultCodeAsStr()
{
    const auto result = getResult();
    const auto code = getInnerCode(result);
    return xdr::xdr_traits<ManageContractResultCode>::enum_name(code);
}

bool
ManageContractOpFrame::doApply(Application& app, LedgerDelta& delta,
                                LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();
    AccountEntry& account = mSourceAccount->getAccount();

    innerResult().code(ManageContractResultCode::SUCCESS);

    auto contractHelper = ContractHelper::Instance();
    auto contractFrame = contractHelper->loadContract(mManageContract.contractID, db, &delta);

    if (!contractFrame)
    {
        innerResult().code(ManageContractResultCode::NOT_FOUND);
        return false;
    }

    if (!(contractFrame->getContractor() == getSourceID()) &&
        !(contractFrame->getCustomer() == getSourceID()))
    {
        innerResult().code(ManageContractResultCode::NOT_ALLOWED);
        return false;
    }

    switch (mManageContract.data.action())
    {
        case ManageContractAction::ADD_DETAILS:
            if (!checkContractDetails(contractFrame, app, db, delta))
                return false;

            contractFrame->addContractDetails(mManageContract.data.details());
            innerResult().response().data.action(ManageContractAction::ADD_DETAILS);
            break;
        case ManageContractAction::START_DISPUTE:
            innerResult().response().data.action(ManageContractAction::START_DISPUTE);
            break;
        case ManageContractAction::CONFIRM_COMPLETED:
            if (!confirmCompleted(contractFrame))
                return false;

            innerResult().response().data.action(ManageContractAction::CONFIRM_COMPLETED);
            innerResult().response().data.isCompleted() = false;
            if (contractFrame->getStatus() == ContractStatus::BOTH_CONFIRMED)
                innerResult().response().data.isCompleted() = true;

            break;
        default:
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected source account. "
                                                   << "Expected contractor, customer or master";
            throw std::runtime_error("Unexpected source account. Expected contractor, customer or master");
    }

    contractHelper->storeChange(delta, db, contractFrame->mEntry);

    return true;
}

bool
ManageContractOpFrame::checkContractDetails(ContractFrame::pointer contractFrame,
                                            Application& app, Database& db, LedgerDelta& delta)
{
    auto maxContractDetails = obtainMaxContractDetailsCount(app, db, delta);

    if (contractFrame->getContractDetailsNumber() >= maxContractDetails)
    {
        innerResult().code(ManageContractResultCode::TOO_MANY_CONTRACT_DETAILS);
        return false;
    }

    auto maxContractDetailLength = obtainMaxContractDetailLength(app, db, delta);

    if (mManageContract.data.details().size() > maxContractDetailLength)
    {
        innerResult().code(ManageContractResultCode::DETAILS_TOO_LONG);
        return false;
    }

    return true;
}

uint64_t
ManageContractOpFrame::obtainMaxContractDetailsCount(Application& app, Database& db, LedgerDelta& delta)
{
    auto maxContractDetailsKey = ManageKeyValueOpFrame::makeMaxContractDetailsKey();
    auto maxContractDetailsKeyValue = KeyValueHelper::Instance()->loadKeyValue(maxContractDetailsKey, db, &delta);

    if (!!maxContractDetailsKeyValue)
    {
        if (maxContractDetailsKeyValue->getKeyValueEntryType() != KeyValueEntryType::UINT32)
        {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected database state. "
                << "Expected max contract details key value to be UINT32. Actual: "
                << xdr::xdr_traits<KeyValueEntryType>::enum_name(maxContractDetailsKeyValue->getKeyValueEntryType());
            throw std::runtime_error("Unexpected database state, expected max contract details key value to be UINT32");
        }

        return maxContractDetailsKeyValue->getKeyValue().value.ui32Value();
    }

    return app.getMaxContractDetailsCount();
}

uint64_t
ManageContractOpFrame::obtainMaxContractDetailLength(Application& app, Database& db, LedgerDelta& delta)
{
    auto maxContractDetailLengthKey = ManageKeyValueOpFrame::makeMaxContractDetailLengthKey();
    auto maxContractDetailLengthKetValue = KeyValueHelper::Instance()->
            loadKeyValue(maxContractDetailLengthKey, db, &delta);

    if (!!maxContractDetailLengthKetValue)
    {
        if (maxContractDetailLengthKetValue->getKeyValueEntryType() != KeyValueEntryType::UINT32)
        {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected database state. "
                << "Expected max contract detail length key value to be UINT32. Actual: "
                << xdr::xdr_traits<KeyValueEntryType>::enum_name(maxContractDetailLengthKetValue->getKeyValueEntryType());
            throw std::runtime_error("Unexpected database state, expected max contract detail length key value to be UINT32");
        }

        return maxContractDetailLengthKetValue->getKeyValue().value.ui32Value();
    }

    return app.getMaxContractDetailLength();
}

bool
ManageContractOpFrame::confirmCompleted(ContractFrame::pointer contractFrame)
{
    if (contractFrame->getStatus() == ContractStatus::BOTH_CONFIRMED)
    {
        innerResult().code(ManageContractResultCode::ALREADY_BOTH_CONFIRMED);
        return false;
    }

    if (contractFrame->getContractor() == getSourceID())
    {
        if (contractFrame->addContractorConfirmation())
        {
            innerResult().code(ManageContractResultCode::ALREADY_CONTRACTOR_CONFIRMED);
            return false;
        }

        return true;
    }
    else if (contractFrame->getCustomer() == getSourceID())
    {
        if (contractFrame->addCustomerConfirmation())
        {
            innerResult().code(ManageContractResultCode::ALREADY_CUSTOMER_CONFIRMED);
            return false;
        }

        return true;
    }
    else if (contractFrame->getJudge() == getSourceID())
    {

    }
    else
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected source account. "
                                               << "Expected contractor, customer or master";
        throw std::runtime_error("Unexpected source account. Expected contractor, customer or master");
    }
}

bool
ManageContractOpFrame::doCheckValid(Application& app)
{
    if (((mManageContract.data.action() == ManageContractAction::ADD_DETAILS) &&
         mManageContract.data.details().empty()) ||
        ((mManageContract.data.action() == ManageContractAction::START_DISPUTE) &&
         mManageContract.data.disputeReason().empty()))
    {
        innerResult().code(ManageContractResultCode::MALFORMED);
        return false;
    }

    return true;
}

}
