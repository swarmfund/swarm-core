// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/asio.h"
#include "ManageOfferOpFrame.h"
#include "OfferExchange.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "ledger/AccountHelper.h"
#include "ledger/AssetPairHelper.h"
#include "ledger/BalanceHelperLegacy.h"
#include "main/Application.h"
#include "util/Logging.h"
#include "util/types.h"
#include "DeleteOfferOpFrame.h"
#include "CreateOfferOpFrame.h"
#include "DeleteSaleParticipationOpFrame.h"
#include "CreateSaleParticipationOpFrame.h"

namespace stellar
{
using namespace std;
using xdr::operator==;

ManageOfferOpFrame::ManageOfferOpFrame(Operation const& op,
                                       OperationResult& res,
                                       TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mManageOffer(mOperation.body.manageOfferOp())
{
}

ManageOfferOpFrame* ManageOfferOpFrame::make(Operation const& op,
                                             OperationResult& res,
                                             TransactionFrame& parentTx)
{
    const auto manageOffer = op.body.manageOfferOp();
    if (manageOffer.orderBookID == SECONDARY_MARKET_ORDER_BOOK_ID)
    {
        if (manageOffer.amount == 0)
        {
            return new DeleteOfferOpFrame(op, res, parentTx);
        }

        return new CreateOfferOpFrame(op, res, parentTx);
    }

    if (manageOffer.amount == 0)
    {
        return new DeleteSaleParticipationOpFrame(op, res, parentTx);
    }

    return new CreateSaleParticipationOpFrame(op, res, parentTx);
}

std::string ManageOfferOpFrame::getInnerResultCodeAsStr()
{
    const auto result = getResult();
    const auto code = getInnerCode(result);
    return xdr::xdr_traits<ManageOfferResultCode>::enum_name(code);
}

std::unordered_map<AccountID, CounterpartyDetails> ManageOfferOpFrame::
getCounterpartyDetails(Database& db, LedgerDelta* delta) const
{
    // no counterparties
    return {};
}

SourceDetails ManageOfferOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                                          int32_t ledgerVersion)
const
{
    uint32_t allowedBlockedReasons = 0;
    if (mManageOffer.offerID != 0 && mManageOffer.amount == 0)
        allowedBlockedReasons = getAnyBlockReason();
    return SourceDetails({
                             AccountType::GENERAL, AccountType::NOT_VERIFIED,
                             AccountType::SYNDICATE, AccountType::EXCHANGE, AccountType::VERIFIED,
                             AccountType::ACCREDITED_INVESTOR, AccountType::INSTITUTIONAL_INVESTOR
                         },
                         mSourceAccount->getMediumThreshold(),
                         static_cast<int32_t>(SignerType::BALANCE_MANAGER),
                         allowedBlockedReasons);
}
}
