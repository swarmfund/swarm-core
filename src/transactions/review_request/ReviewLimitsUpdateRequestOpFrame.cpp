// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/asio.h"
#include "ReviewLimitsUpdateRequestOpFrame.h"
#include "util/Logging.h"
#include "util/types.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "ledger/AccountHelper.h"
#include "ledger/AccountLimitsHelper.h"
#include "transactions/SetLimitsOpFrame.h"
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
        return SourceDetails({}, mSourceAccount->getHighThreshold(),
                             static_cast<int32_t >(SignerType::LIMITS_MANAGER));
    }

    bool ReviewLimitsUpdateRequestOpFrame::tryCallSetLimits(Application &app,
                                                            LedgerManager &ledgerManager, LedgerDelta &delta,
                                                            ReviewableRequestFrame::pointer request)
    {
        Database& db = ledgerManager.getDatabase();
        auto requestorID = request->getRequestor();

        Operation op;
        op.body.type(OperationType::SET_LIMITS);
        SetLimitsOp& setLimitsOp = op.body.setLimitsOp();
        setLimitsOp.account.activate() = requestorID;
        setLimitsOp.limits = mReviewRequest.requestDetails.limitsUpdate().newLimits;

        OperationResult opRes;
        opRes.code(OperationResultCode::opINNER);
        opRes.tr().type(OperationType::SET_LIMITS);
        SetLimitsOpFrame setLimitsOpFrame(op, opRes, mParentTx);

        auto accountHelper = AccountHelper::Instance();
        auto master = accountHelper->mustLoadAccount(app.getMasterID(), db);
        setLimitsOpFrame.setSourceAccountPtr(master);

        if (!setLimitsOpFrame.doCheckValid(app) || !setLimitsOpFrame.doApply(app, delta, ledgerManager))
        {
            auto resultCodeString = setLimitsOpFrame.getInnerResultCodeAsStr();
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected state: failed to apply set limits on review limits update request: "
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
        if (request->getRequestType() != ReviewableRequestType::LIMITS_UPDATE) {
            CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected request type. Expected LIMITS_UPDATE, but got " << xdr::xdr_traits<ReviewableRequestType>::enum_name(request->getRequestType());
            throw std::invalid_argument("Unexpected request type for review limits update request");
        }

        Database& db = ledgerManager.getDatabase();
        EntryHelperProvider::storeDeleteEntry(delta, db, request->getKey());

        auto requestorID = request->getRequestor();
        AccountHelper::Instance()->ensureExists(requestorID, db);

        return tryCallSetLimits(app, ledgerManager, delta, request);
    }

    bool ReviewLimitsUpdateRequestOpFrame::handleReject(Application &app, LedgerDelta &delta,
                                                        LedgerManager &ledgerManager,
                                                        ReviewableRequestFrame::pointer request)
    {
        innerResult().code(ReviewRequestResultCode::REJECT_NOT_ALLOWED);
        return false;
    }

}