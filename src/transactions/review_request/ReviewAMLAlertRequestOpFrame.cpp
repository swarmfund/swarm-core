//
// Created by oleg on 02.03.18.
//

#include "ReviewAMLAlertRequestOpFrame.h"
#include <transactions/manage_asset/ManageAssetHelper.h>
#include "util/asio.h"

#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "ledger/AssetHelperLegacy.h"
#include "ledger/BalanceHelperLegacy.h"
#include "main/Application.h"
#include "ledger/ReviewableRequestHelper.h"
#include "xdrpp/printer.h"

namespace stellar {

    using namespace std;
    using xdr::operator==;


ReviewAMLAlertRequestOpFrame::ReviewAMLAlertRequestOpFrame(Operation const& op,
    OperationResult& res, TransactionFrame& parentTx) : ReviewRequestOpFrame(op, res, parentTx)
{
}

bool ReviewAMLAlertRequestOpFrame::handleApprove(Application& app,
    LedgerDelta& delta, LedgerManager& ledgerManager,
                                                 const ReviewableRequestFrame::pointer request)
{
    const auto amlAlert = getAmlAlert(request);
    auto& db = app.getDatabase();
    createReference(delta, db, request->getRequestor(), request->getReference());
    auto balanceFrame = BalanceHelperLegacy::Instance()->mustLoadBalance(amlAlert.balanceID, db, &delta);
    if (!balanceFrame->tryChargeFromLocked(amlAlert.amount)) {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: failed to charge from unlock specified amount in request: request"
            << xdr::xdr_to_string(amlAlert) << "; balance: " << xdr::xdr_to_string(balanceFrame->getBalance());
        throw runtime_error("Unexpected state: failed to charge from unlock specified amount in aml request");
    }

    BalanceHelperLegacy::Instance()->storeChange(delta, db, balanceFrame->mEntry);

    auto assetFrame = AssetHelperLegacy::Instance()->mustLoadAsset(balanceFrame->getAsset(), db, &delta);
    if (!assetFrame->tryWithdraw(amlAlert.amount))
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: failed to withdraw from asset for aml alert request: " << request->getRequestID();
        throw runtime_error("Failed to withdraw from asset for aml alert");
    }

    AssetHelperLegacy::Instance()->storeChange(delta, db, assetFrame->mEntry);
    ReviewableRequestHelper::Instance()->storeDelete(delta, db, request->getKey());
    innerResult().code(ReviewRequestResultCode::SUCCESS);
    return true;
}

bool ReviewAMLAlertRequestOpFrame::handleReject(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager,
                                                    ReviewableRequestFrame::pointer request) {
        innerResult().code(ReviewRequestResultCode::REJECT_NOT_ALLOWED);
        return false;
    }

    SourceDetails ReviewAMLAlertRequestOpFrame::getSourceAccountDetails(
            std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails, int32_t ledgerVersion) const {
        return SourceDetails({AccountType::MASTER}, mSourceAccount->getHighThreshold(),
                             static_cast<int32_t>(SignerType::AML_ALERT_REVIEWER));
    }

AMLAlertRequest ReviewAMLAlertRequestOpFrame::getAmlAlert(
    ReviewableRequestFrame::pointer request)
{
    auto requestEntry = request->getRequestEntry();
    if (requestEntry.body.type() != ReviewableRequestType::AML_ALERT)
    {
        CLOG(ERROR, Logging::OPERATION_LOGGER) << "Review aml alert: expected aml alert but got " << xdr::xdr_to_string(requestEntry.body.type());
        throw runtime_error("Expected request to be AML alert");
    }
    return requestEntry.body.amlAlertRequest();
}

bool ReviewAMLAlertRequestOpFrame::handlePermanentReject(Application &app, LedgerDelta &delta,
                                                             LedgerManager &ledgerManager,
                                                         const ReviewableRequestFrame::pointer request) {
        auto& db = app.getDatabase();
        // create reference to make sure that same alert will not be triggered again
        createReference(delta, db, request->getRequestor(), request->getReference());
        const auto amlAlert = getAmlAlert(request);
        auto balanceFrame = BalanceHelperLegacy::Instance()->mustLoadBalance(amlAlert.balanceID,db,&delta);
        if(!balanceFrame->unlock(amlAlert.amount)){
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: failed to unlock specified amount in request: request" 
            << xdr::xdr_to_string(amlAlert) << "; balance: " << xdr::xdr_to_string(balanceFrame->getBalance());
            throw runtime_error("Unexpected state: failed to unlock specified amount in aml request");
        }

    BalanceHelperLegacy::Instance()->storeChange(delta,db,balanceFrame->mEntry);
        ReviewableRequestHelper::Instance()->storeDelete(delta,db,request->getKey());
        innerResult().code(ReviewRequestResultCode::SUCCESS);
        return true;
    }

}
