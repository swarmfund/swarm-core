#include <ledger/EntryHelperLegacy.h>
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
    return SourceDetails(getAllAccountTypes(), mSourceAccount->getMediumThreshold(),
                         static_cast<int32_t>(SignerType::LIMITS_MANAGER),
                         static_cast<uint32_t>(BlockReasons::WITHDRAWAL)
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

std::string
CreateManageLimitsRequestOpFrame::getLimitsManageRequestDetailsReference(longstring const& details) const
{
    const auto hash = sha256(xdr::xdr_to_opaque(ReviewableRequestType::LIMITS_UPDATE, details));
    return binToHex(hash);
}

bool CreateManageLimitsRequestOpFrame::updateManageLimitsRequest(LedgerManager &lm, Database &db, LedgerDelta &delta) {
    auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
    auto requestFrame = reviewableRequestHelper->loadRequest(mCreateManageLimitsRequest.ext.requestID(), getSourceID(),
                                                             ReviewableRequestType::LIMITS_UPDATE, db, &delta);
    if (!requestFrame)
    {
        innerResult().code(CreateManageLimitsRequestResultCode::MANAGE_LIMITS_REQUEST_NOT_FOUND);
        return false;
    }

    auto& limitsUpdateRequest = requestFrame->getRequestEntry().body.limitsUpdateRequest();
    limitsUpdateRequest.ext.details() = mCreateManageLimitsRequest.manageLimitsRequest.ext.details();

    requestFrame->recalculateHashRejectReason();
    reviewableRequestHelper->storeChange(delta, db, requestFrame->mEntry);

    innerResult().code(CreateManageLimitsRequestResultCode::SUCCESS);
    innerResult().success().manageLimitsRequestID = requestFrame->getRequestID();

    return true;
}

bool CreateManageLimitsRequestOpFrame::createManageLimitsRequest(Application &app, LedgerDelta &delta,
                                                                 LedgerManager &ledgerManager)
{
    Database& db = ledgerManager.getDatabase();

    longstring reference;
    auto& manageLimitsRequest = mCreateManageLimitsRequest.manageLimitsRequest;
    if (ledgerManager.shouldUse(LedgerVersion::LIMITS_UPDATE_REQUEST_DEPRECATED_DOCUMENT_HASH) &&
        manageLimitsRequest.ext.v() == LedgerVersion::LIMITS_UPDATE_REQUEST_DEPRECATED_DOCUMENT_HASH)
    {
        auto details = manageLimitsRequest.ext.details();
        reference = getLimitsManageRequestDetailsReference(details);
    }
    else
        reference = getLimitsManageRequestReference(mCreateManageLimitsRequest.manageLimitsRequest.deprecatedDocumentHash);

    const auto referencePtr = xdr::pointer<string64>(new string64(reference));

    auto reviewableRequestHelper = ReviewableRequestHelper::Instance();
    if (reviewableRequestHelper->isReferenceExist(db, getSourceID(), reference))
    {
        innerResult().code(CreateManageLimitsRequestResultCode::MANAGE_LIMITS_REQUEST_REFERENCE_DUPLICATION);
        return false;
    }

    ReviewableRequestEntry::_body_t body;
    body.type(ReviewableRequestType::LIMITS_UPDATE);
    if (ledgerManager.shouldUse(LedgerVersion::LIMITS_UPDATE_REQUEST_DEPRECATED_DOCUMENT_HASH))
    {
        body.limitsUpdateRequest().ext.v(LedgerVersion::LIMITS_UPDATE_REQUEST_DEPRECATED_DOCUMENT_HASH);
        body.limitsUpdateRequest().ext.details() = mCreateManageLimitsRequest.manageLimitsRequest.ext.details();
    }
    else
        body.limitsUpdateRequest().deprecatedDocumentHash =
                mCreateManageLimitsRequest.manageLimitsRequest.deprecatedDocumentHash;

    auto request = ReviewableRequestFrame::createNewWithHash(delta, getSourceID(), app.getMasterID(), referencePtr,
                                                             body, ledgerManager.getCloseTime());

    EntryHelperProvider::storeAddEntry(delta, db, request->mEntry);

    innerResult().success().manageLimitsRequestID = request->getRequestID();

    return true;
}

bool
CreateManageLimitsRequestOpFrame::doApply(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager)
{
    if(!ledgerManager.shouldUse(mCreateManageLimitsRequest.ext.v()))
    {
        innerResult().code(CreateManageLimitsRequestResultCode::INVALID_MANAGE_LIMITS_REQUEST_VERSION);
        return false;
    }

    if (!ledgerManager.shouldUse(LedgerVersion::ALLOW_TO_UPDATE_AND_REJECT_LIMITS_UPDATE_REQUESTS))
    {
        return createManageLimitsRequest(app, delta, ledgerManager);
    }

    auto& manageLimitsRequest = mCreateManageLimitsRequest.manageLimitsRequest;
    bool requestHasNewDetails = manageLimitsRequest.ext.v() ==
                                LedgerVersion::LIMITS_UPDATE_REQUEST_DEPRECATED_DOCUMENT_HASH;

    if (requestHasNewDetails && !isValidJson(manageLimitsRequest.ext.details()))
    {
        innerResult().code(CreateManageLimitsRequestResultCode::INVALID_DETAILS);
        return false;
    }

    // required for the new flow, when source have to specify request id for creation or update of the request
    bool isUpdating = mCreateManageLimitsRequest.ext.v() ==
                      LedgerVersion::ALLOW_TO_UPDATE_AND_REJECT_LIMITS_UPDATE_REQUESTS &&
                      mCreateManageLimitsRequest.ext.requestID() != 0;
    if (isUpdating)
    {
        auto& db = app.getDatabase();
        return updateManageLimitsRequest(ledgerManager, db, delta);
    }

    return createManageLimitsRequest(app, delta, ledgerManager);
}

bool CreateManageLimitsRequestOpFrame::doCheckValid(Application& app)
{
    return true;
}

}