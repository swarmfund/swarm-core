// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <transactions/manage_asset/ManageAssetHelper.h>
#include "util/asio.h"
#include "ReviewInvoiceRequestOpFrame.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "ledger/AssetHelper.h"
#include "ledger/BalanceHelper.h"
#include "main/Application.h"

namespace stellar
{

using namespace std;
using xdr::operator==;


SourceDetails
ReviewInvoiceRequestOpFrame::getSourceAccountDetails(
        unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails, int32_t ledgerVersion) const
{
    return SourceDetails({AccountType::MASTER}, mSourceAccount->getHighThreshold(),
                         static_cast<int32_t >(SignerType::INVOICE_MANAGER));
}

bool ReviewInvoiceRequestOpFrame::handleApprove(Application& app, LedgerDelta& delta,
                                                LedgerManager& ledgerManager,
                                                ReviewableRequestFrame::pointer request)
{
    innerResult().code(ReviewRequestResultCode::APPROVE_NOT_ALLOWED);
    return false;
}

bool
ReviewInvoiceRequestOpFrame::handleReject(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager,
                                          ReviewableRequestFrame::pointer request)
{
    innerResult().code(ReviewRequestResultCode::REJECT_NOT_ALLOWED);
    return false;
}

ReviewInvoiceRequestOpFrame::ReviewInvoiceRequestOpFrame(Operation const & op, OperationResult & res,
                                                         TransactionFrame & parentTx) :
        ReviewRequestOpFrame(op, res, parentTx)
{
}

}
