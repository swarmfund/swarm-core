// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ManageSaleParticipationOpFrame.h"
#include "ledger/LedgerDelta.h"
#include "database/Database.h"

namespace stellar
{
using namespace std;
using xdr::operator==;


ManageSaleParticipationOpFrame::ManageSaleParticipationOpFrame(
    Operation const& op, OperationResult& res, TransactionFrame& parentTx) : ManageOfferOpFrame(op, res, parentTx)
{
}
}
