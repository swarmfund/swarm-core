//
// Created by Roman on 02.03.18.
//

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/CreateAMLAlertRequestOpFrame.h"
#include "database/Database.h"
#include "main/Application.h"
#include "medida/metrics_registry.h"
#include "ledger/LedgerDelta.h"
#include "ledger/AccountHelper.h"
#include "ledger/BalanceHelper.h"
#include "ledger/ReviewableRequestFrame.h"
#include "xdrpp/printer.h"
#include "ledger/ReviewableRequestHelper.h"
#include "bucket/BucketApplicator.h"


namespace stellar {
    using xdr::operator==;


    std::unordered_map<AccountID, CounterpartyDetails>
    CreateAMLAlertRequestOpFrame::getCounterpartyDetails(
            Database &db, LedgerDelta *delta) const {
        BalanceFrame::pointer balanceFrame = BalanceHelper::Instance()->loadBalance(mCreateAMLAlertRequest.amlAlertRequest.balanceID, db,
                                                                                    delta);

        return {
                {balanceFrame->getAccountID(), CounterpartyDetails({AccountType::GENERAL, AccountType::SYNDICATE,
                                                         AccountType::EXCHANGE,AccountType::NOT_VERIFIED}, false, true)}
        };;
    }

    SourceDetails CreateAMLAlertRequestOpFrame::getSourceAccountDetails(
            std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
            int32_t ledgerVersion)
    const {
        return SourceDetails({
                                     AccountType::MASTER,
                             }, mSourceAccount->getHighThreshold(),
                             static_cast<int32_t>(SignerType::AML_ALERT_MANAGER));
    }


    CreateAMLAlertRequestOpFrame::CreateAMLAlertRequestOpFrame(
            Operation const &op, OperationResult &res,
            TransactionFrame &parentTx)
            : OperationFrame(op, res, parentTx), mCreateAMLAlertRequest(mOperation.body.createAMLAlertRequestOp()) {
    }


    bool
    CreateAMLAlertRequestOpFrame::doApply(Application &app, LedgerDelta &delta,
                                          LedgerManager &ledgerManager) {
        auto &db = ledgerManager.getDatabase();
        AMLAlertRequest amlAlertRequest = mCreateAMLAlertRequest.amlAlertRequest;
        BalanceFrame::pointer balanceFrame = BalanceHelper::Instance()->loadBalance(amlAlertRequest.balanceID, db,
                                                                                    &delta);
        if (!balanceFrame) {
            innerResult().code(CreateAMLAlertRequestResultCode::BALANCE_NOT_EXIST);
            return false;
        }
        auto result = balanceFrame->tryLock(amlAlertRequest.amount);
        if (result != BalanceFrame::Result::SUCCESS) {
            innerResult().code(CreateAMLAlertRequestResultCode::INVALID_AMOUNT);
            return false;
        }

        uint64 requestID = delta.getHeaderFrame().generateID(LedgerEntryType::REVIEWABLE_REQUEST);
        const auto referencePtr = xdr::pointer<string64>(new string64(mCreateAMLAlertRequest.reference));
        auto requestFrame = ReviewableRequestFrame::createNew(requestID, getSourceID(), app.getMasterID(), referencePtr,
                                                              ledgerManager.getCloseTime());

        auto &requestEntry = requestFrame->getRequestEntry();
        requestEntry.body.type(ReviewableRequestType::AML_ALERT);
        requestEntry.body.amlAlertRequest() = amlAlertRequest;
        requestFrame->recalculateHashRejectReason();
        BalanceHelper::Instance()->storeChange(delta,db,balanceFrame->mEntry);
        ReviewableRequestHelper::Instance()->storeAdd(delta, db, requestFrame->mEntry);
        innerResult().code(CreateAMLAlertRequestResultCode::SUCCESS);
        innerResult().success().requestID = requestID;
        return true;
    }

    bool CreateAMLAlertRequestOpFrame::doCheckValid(Application &app) {


        if (mCreateAMLAlertRequest.amlAlertRequest.reason.empty()) {
            innerResult().code(CreateAMLAlertRequestResultCode::INVALID_REASON);
            return false;
        }

        return true;
    }
}
