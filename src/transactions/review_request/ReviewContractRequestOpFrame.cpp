// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <transactions/manage_asset/ManageAssetHelper.h>
#include <transactions/payment/PaymentOpV2Frame.h>
#include <transactions/ManageContractOpFrame.h>
#include "util/asio.h"
#include "ReviewContractRequestOpFrame.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "ledger/AssetHelperLegacy.h"
#include "ledger/ContractHelper.h"
#include "ledger/BalanceHelperLegacy.h"
#include "ledger/LedgerHeaderFrame.h"
#include "main/Application.h"

namespace stellar
{

using namespace std;
using xdr::operator==;


SourceDetails
ReviewContractRequestOpFrame::getSourceAccountDetails(
        unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails, int32_t ledgerVersion) const
{
    return SourceDetails(getAllAccountTypes(), mSourceAccount->getHighThreshold(),
                         static_cast<int32_t>(SignerType::CONTRACT_MANAGER));
}

bool
ReviewContractRequestOpFrame::handleApprove(Application& app, LedgerDelta& delta,
                                           LedgerManager& ledgerManager,
                                           ReviewableRequestFrame::pointer request)
{
    innerResult().code(ReviewRequestResultCode::SUCCESS);

    if (request->getRequestType() != ReviewableRequestType::CONTRACT)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected request type. Expected CONTRACT, but got "
                         << xdr::xdr_traits<ReviewableRequestType>::enum_name(request->getRequestType());
        throw invalid_argument("Unexpected request type for review contract request");
    }

    Database& db = ledgerManager.getDatabase();

    EntryHelperProvider::storeDeleteEntry(delta, db, request->getKey());

    auto contractFrame = make_shared<ContractFrame>();
    auto& contractEntry = contractFrame->getContract();
    auto contractRequest = request->getRequestEntry().body.contractRequest();

    contractEntry.contractID = delta.getHeaderFrame().generateID(LedgerEntryType::CONTRACT);
    contractEntry.contractor = request->getRequestor();
    contractEntry.customer = request->getReviewer();
    contractEntry.escrow = contractRequest.escrow;
    contractEntry.startTime = contractRequest.startTime;
    contractEntry.endTime = contractRequest.endTime;
    contractEntry.initialDetails = contractRequest.details;
    contractEntry.state = static_cast<uint32_t>(ContractState::NO_CONFIRMATIONS);

    if (ledgerManager.shouldUse(LedgerVersion::ADD_CUSTOMER_DETAILS_TO_CONTRACT))
    {
        if (!checkCustomerDetailsLength(app, db, delta))
            return false;

        contractEntry.ext.v(LedgerVersion::ADD_CUSTOMER_DETAILS_TO_CONTRACT);
        contractEntry.ext.customerDetails() = mReviewRequest.requestDetails.contract().details;
    }

    EntryHelperProvider::storeAddEntry(delta, db, contractFrame->mEntry);

    if (ledgerManager.shouldUse(LedgerVersion::ADD_CONTRACT_ID_REVIEW_REQUEST_RESULT))
    {
        innerResult().success().ext.v(LedgerVersion::ADD_CONTRACT_ID_REVIEW_REQUEST_RESULT);
        innerResult().success().ext.contractID() = contractEntry.contractID;
    }

    return true;
}

bool
ReviewContractRequestOpFrame::checkCustomerDetailsLength(Application& app, Database& db, LedgerDelta& delta)
{
    auto maxCustomerDetailsLength = ManageContractOpFrame::obtainMaxContractDetailLength(app, db, delta);
    if (mReviewRequest.requestDetails.contract().details.size() > maxCustomerDetailsLength)
    {
        innerResult().code(ReviewRequestResultCode::CONTRACT_DETAILS_TOO_LONG);
        return false;
    }
    
    return true;
}

bool
ReviewContractRequestOpFrame::handleReject(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager,
                                           ReviewableRequestFrame::pointer request)
{
    innerResult().code(ReviewRequestResultCode::REJECT_NOT_ALLOWED);
    return false;
}

ReviewContractRequestOpFrame::ReviewContractRequestOpFrame(Operation const & op, OperationResult & res,
                                                           TransactionFrame & parentTx)
        : ReviewRequestOpFrame(op, res, parentTx)
{
}

}
