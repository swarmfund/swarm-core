//
// Created by artem on 16.06.18.
//

#include <ledger/EntryHelper.h>
#include <ledger/ReviewableRequestFrame.h>
#include <crypto/SHA.h>
#include <ledger/ReviewableRequestHelper.h>
#include <lib/xdrpp/xdrpp/marshal.h>
#include "CreateManageLimitsRequestOpFrame.h"
#include "main/Application.h"


namespace stellar
{

std::unordered_map<AccountID, CounterpartyDetails>
CreateManageLimitsRequestOpFrame::getCounterpartyDetails(Database& db, LedgerDelta* delta) const
{
    return {};
}

SourceDetails
CreateManageLimitsRequestOpFrame::getSourceAccountDetails(
        std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
        int32_t ledgerVersion) const
{
    return SourceDetails({AccountType::MASTER, AccountType::GENERAL, AccountType::NOT_VERIFIED,
                          AccountType::SYNDICATE, AccountType::EXCHANGE, AccountType::ACCREDITED_INVESTOR,
                          AccountType::COMMISSION, AccountType::INSTITUTIONAL_INVESTOR, AccountType::OPERATIONAL
                         }, mSourceAccount->getMediumThreshold(),
                         static_cast<int32_t>(SignerType::LIMITS_MANAGER)
    );
}

CreateManageLimitsRequestOpFrame::CreateManageLimitsRequestOpFrame(
        Operation const& op, OperationResult& res,
        TransactionFrame& parentTx)
        : OperationFrame(op, res, parentTx)
        , mCreateManageLimitsRequest(mOperation.body.createManageLimitsRequestOp()){}

std::string
CreateManageLimitsRequestOpFrame::getLimitsManageRequestReference(Hash const& documentHash) const
{
    const auto hash = sha256(xdr::xdr_to_opaque(ReviewableRequestType::LIMITS_UPDATE, documentHash));
    return binToHex(hash);
}

bool
CreateManageLimitsRequestOpFrame::doApply(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();

    auto reference = getLimitsManageRequestReference(mCreateManageLimitsRequest.manageLimitsRequest.documentHash);
    const auto referencePtr = xdr::pointer<string64>(new string64(reference));

    auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
    if (reviewableRequestHelper->isReferenceExist(db, getSourceID(), reference))
    {
        innerResult().code(CreateManageLimitsRequestResultCode::MANAGE_LIMITS_REQUEST_REFERENCE_DUPLICATION);
        return false;
    }

    ReviewableRequestEntry::_body_t body;
    body.type(ReviewableRequestType::LIMITS_UPDATE);
    body.limitsUpdateRequest().documentHash = mCreateManageLimitsRequest.manageLimitsRequest.documentHash;

    auto request = ReviewableRequestFrame::createNewWithHash(delta, getSourceID(), app.getMasterID(), referencePtr,
                                                             body, ledgerManager.getCloseTime());

    EntryHelperProvider::storeAddEntry(delta, db, request->mEntry);

    innerResult().success().manageLimitsRequestID = request->getRequestID();

    return true;
}

bool CreateManageLimitsRequestOpFrame::doCheckValid(Application& app)
{
    return true;
}

}