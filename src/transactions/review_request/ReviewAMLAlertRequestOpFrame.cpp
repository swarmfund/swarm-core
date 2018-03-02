//
// Created by oleg on 02.03.18.
//

#include "ReviewAMLAlertRequestOpFrame.h"
#include <transactions/manage_asset/ManageAssetHelper.h>
#include "util/asio.h"

#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "ledger/AssetHelper.h"
#include "ledger/BalanceHelper.h"
#include "main/Application.h"
#include "ledger/ReviewableRequestHelper.h"

namespace stellar {

    using namespace std;
    using xdr::operator==;


    bool ReviewAMLAlertRequestOpFrame::handleReject(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager,
                                                    ReviewableRequestFrame::pointer request) {
        innerResult().code(ReviewRequestResultCode::REJECT_NOT_ALLOWED);
        return false;
    }

    SourceDetails ReviewAMLAlertRequestOpFrame::getSourceAccountDetails(
            std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails, int32_t ledgerVersion) const {
        return SourceDetails({AccountType::MASTER}, mSourceAccount->getHighThreshold(),
                             static_cast<int32_t>(SignerType::AML_ALERT_MANAGER));
    }
    bool ReviewAMLAlertRequestOpFrame::handlePermanentReject(Application &app, LedgerDelta &delta,
                                                             LedgerManager &ledgerManager,
                                                             ReviewableRequestFrame::pointer request) {
        Database & db = app.getDatabase();
        auto requestEntry = request->getRequestEntry();
        auto amlAlertRequest = requestEntry.body.amlAlertRequest()
        auto balanceFrame = BalanceHelper::Instance()->loadBalance(amlAlertRequest.balanceID,db,&delta);
        if(!balanceFrame){
            throw std::runtime_error("balance not found");
        }
        auto result = balanceFrame->unlock(amlAlertRequest.amount);
        if(!result){
            //throw exception
            return false;
        }
        BalanceHelper::Instance()->storeChange(delta,db,balanceFrame->mEntry);
        ReviewableRequestHelper::Instance()->storeDelete(delta,db,request->getKey());
        innerResult().code(ReviewRequestResultCode::SUCCESS);
        return true;
    }

}