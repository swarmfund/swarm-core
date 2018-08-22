// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/asio.h"
#include "ReviewLimitsUpdateRequestOpFrame.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "ledger/AccountHelper.h"
#include <ledger/ReviewableRequestHelper.h>
#include "transactions/ManageLimitsOpFrame.h"
#include "main/Application.h"

namespace stellar {

    using namespace std;
    using xdr::operator==;

    ReviewLimitsUpdateRequestOpFrame::ReviewLimitsUpdateRequestOpFrame(Operation const &op, OperationResult &res,
                                                                       TransactionFrame &parentTx) :
                                      ReviewRequestOpFrame(op, res, parentTx)
    {
    }

    SourceDetails ReviewLimitsUpdateRequestOpFrame::getSourceAccountDetails(
            std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails, int32_t ledgerVersion) const
    {
        return SourceDetails({AccountType::MASTER}, mSourceAccount->getHighThreshold(),
                             static_cast<int32_t >(SignerType::LIMITS_MANAGER));
    }

    bool
    ReviewLimitsUpdateRequestOpFrame::handleManageLimitsResult(ManageLimitsResultCode manageLimitsResultCode)
    {
        switch (manageLimitsResultCode) {
            case ManageLimitsResultCode::CANNOT_CREATE_FOR_ACC_ID_AND_ACC_TYPE: {
                innerResult().code(ReviewRequestResultCode::CANNOT_CREATE_FOR_ACC_ID_AND_ACC_TYPE);
                return false;
            }
            case ManageLimitsResultCode::INVALID_LIMITS: {
                innerResult().code(ReviewRequestResultCode::INVALID_LIMITS);
                return false;
            }
            default: {
                CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected result code from manage limits: "
                                                       << xdr::xdr_traits<ManageLimitsResultCode>::enum_name(manageLimitsResultCode);
                throw std::runtime_error("Unexpected result code from manage limits");
            }
        }
    }

    bool
    ReviewLimitsUpdateRequestOpFrame::tryCallManageLimits(Application &app,
                                                          LedgerManager &ledgerManager, LedgerDelta &delta,
                                                          ReviewableRequestFrame::pointer request)
    {
        Database& db = ledgerManager.getDatabase();
        auto requestorID = request->getRequestor();

        Operation op;
        op.body.type(OperationType::MANAGE_LIMITS);
        ManageLimitsOp& manageLimitsOp = op.body.manageLimitsOp();
        manageLimitsOp.details.action(ManageLimitsAction::CREATE);
        manageLimitsOp.details.limitsCreateDetails().accountID = mReviewRequest.requestDetails.limitsUpdate().newLimitsV2.accountID;
        manageLimitsOp.details.limitsCreateDetails().accountType = mReviewRequest.requestDetails.limitsUpdate().newLimitsV2.accountType;
        manageLimitsOp.details.limitsCreateDetails().statsOpType = mReviewRequest.requestDetails.limitsUpdate().newLimitsV2.statsOpType;
        manageLimitsOp.details.limitsCreateDetails().assetCode = mReviewRequest.requestDetails.limitsUpdate().newLimitsV2.assetCode;
        manageLimitsOp.details.limitsCreateDetails().isConvertNeeded = mReviewRequest.requestDetails.limitsUpdate().newLimitsV2.isConvertNeeded;
        manageLimitsOp.details.limitsCreateDetails().dailyOut = mReviewRequest.requestDetails.limitsUpdate().newLimitsV2.dailyOut;
        manageLimitsOp.details.limitsCreateDetails().weeklyOut = mReviewRequest.requestDetails.limitsUpdate().newLimitsV2.weeklyOut;
        manageLimitsOp.details.limitsCreateDetails().monthlyOut = mReviewRequest.requestDetails.limitsUpdate().newLimitsV2.monthlyOut;
        manageLimitsOp.details.limitsCreateDetails().annualOut = mReviewRequest.requestDetails.limitsUpdate().newLimitsV2.annualOut;

        OperationResult opRes;
        opRes.code(OperationResultCode::opINNER);
        opRes.tr().type(OperationType::MANAGE_LIMITS);
        ManageLimitsOpFrame manageLimitsOpFrame(op, opRes, mParentTx);

        auto accountHelper = AccountHelper::Instance();
        auto master = accountHelper->mustLoadAccount(app.getMasterID(), db);
        manageLimitsOpFrame.setSourceAccountPtr(master);

        if (!manageLimitsOpFrame.doCheckValid(app) || !manageLimitsOpFrame.doApply(app, delta, ledgerManager))
        {
            if (ledgerManager.shouldUse(LedgerVersion::ALLOW_TO_UPDATE_AND_REJECT_LIMITS_UPDATE_REQUESTS))
            {
                OperationResult& manageLimitsResult = manageLimitsOpFrame.getResult();
                auto manageLimitsResultCode = ManageLimitsOpFrame::getInnerCode(manageLimitsResult);
                return handleManageLimitsResult(manageLimitsResultCode);
            }

            auto resultCodeString = manageLimitsOpFrame.getInnerResultCodeAsStr();
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: failed to apply manage limits"
                                                      " on review limits update request: "
                                                   << request->getRequestID() << " with code: " << resultCodeString;
            throw runtime_error("Unexpected state: failed to set limits on review limits update request with code: " +
                                 resultCodeString);
        }

        auto reference = request->getReference();

        createReference(delta, db, requestorID, reference);

        innerResult().code(ReviewRequestResultCode::SUCCESS);
        return true;
    }

    bool ReviewLimitsUpdateRequestOpFrame::handleApprove(Application &app, LedgerDelta &delta,
                                                         LedgerManager &ledgerManager,
                                                         ReviewableRequestFrame::pointer request)
    {
        request->checkRequestType(ReviewableRequestType::LIMITS_UPDATE);

        Database& db = ledgerManager.getDatabase();
        EntryHelperProvider::storeDeleteEntry(delta, db, request->getKey());

        auto requestorID = request->getRequestor();
        AccountHelper::Instance()->ensureExists(requestorID, db);

        return tryCallManageLimits(app, ledgerManager, delta, request);
    }

    bool ReviewLimitsUpdateRequestOpFrame::handleReject(Application &app, LedgerDelta &delta,
                                                        LedgerManager &ledgerManager,
                                                        ReviewableRequestFrame::pointer request)
    {
        if (!ledgerManager.shouldUse(LedgerVersion::ALLOW_TO_UPDATE_AND_REJECT_LIMITS_UPDATE_REQUESTS))
        {
            innerResult().code(ReviewRequestResultCode::REJECT_NOT_ALLOWED);
            return false;
        }

        request->checkRequestType(ReviewableRequestType::LIMITS_UPDATE);
        request->setRejectReason(mReviewRequest.reason);

        auto& requestEntry = request->getRequestEntry();
        const auto newHash = ReviewableRequestFrame::calculateHash(requestEntry.body);
        requestEntry.hash = newHash;

        Database& db = app.getDatabase();
        ReviewableRequestHelper::Instance()->storeChange(delta, db, request->mEntry);

        innerResult().code(ReviewRequestResultCode::SUCCESS);
        return true;
    }

}