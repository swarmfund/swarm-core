// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/asio.h"
#include "ReviewAssetCreationRequestOpFrame.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "ledger/AssetHelper.h"
#include "ledger/BalanceHelper.h"
#include "main/Application.h"

namespace stellar
{

using namespace std;
using xdr::operator==;

void ReviewAssetCreationRequestOpFrame::createSystemBalances(AssetCode assetCode, Application &app, LedgerDelta &delta,
                                                             uint64_t ledgerCloseTime)
{
    auto systemAccounts = app.getSystemAccounts();

    for (auto& systemAccount : systemAccounts)
    {
		auto balanceHelper = BalanceHelper::Instance();
        auto balanceFrame = balanceHelper->loadBalance(systemAccount, assetCode, app.getDatabase(), &delta);
        if (!balanceFrame) {
            BalanceID balanceID = BalanceKeyUtils::forAccount(systemAccount,
                                                              delta.getHeaderFrame().generateID(LedgerEntryType::BALANCE));
            balanceFrame = BalanceFrame::createNew(balanceID, systemAccount, assetCode, ledgerCloseTime);

            EntryHelperProvider::storeAddEntry(delta, app.getDatabase(), balanceFrame->mEntry);
        }
    }
}

bool ReviewAssetCreationRequestOpFrame::handleApprove(Application & app, LedgerDelta & delta, LedgerManager & ledgerManager, ReviewableRequestFrame::pointer request)
{
	if (request->getRequestType() != ReviewableRequestType::ASSET_CREATE) {
		CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected request type. Expected ASSET_CREATE, but got " << xdr::xdr_traits<ReviewableRequestType>::enum_name(request->getRequestType());
		throw std::invalid_argument("Unexpected request type for review asset creation request");
	}

	auto assetCreationRequest = request->getRequestEntry().body.assetCreationRequest();
	Database& db = ledgerManager.getDatabase();

	auto assetHelper = AssetHelper::Instance();
	auto isAssetExist = assetHelper->exists(db, assetCreationRequest.code);
	if (isAssetExist) {
		innerResult().code(ReviewRequestResultCode::ASSET_ALREADY_EXISTS);
		return false;
	}

	auto assetFrame = AssetFrame::create(assetCreationRequest, request->getRequestor());
	EntryHelperProvider::storeAddEntry(delta, db, assetFrame->mEntry);

    if (assetFrame->checkPolicy(AssetPolicy::BASE_ASSET))
        createSystemBalances(assetFrame->getCode(), app, delta, ledgerManager.getCloseTime());

	EntryHelperProvider::storeDeleteEntry(delta, db, request->getKey());
	innerResult().code(ReviewRequestResultCode::SUCCESS);
	return true;
}

SourceDetails ReviewAssetCreationRequestOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const
{
	return SourceDetails({AccountType::MASTER}, mSourceAccount->getHighThreshold(),
						 static_cast<int32_t>(SignerType::ASSET_MANAGER));
}

ReviewAssetCreationRequestOpFrame::ReviewAssetCreationRequestOpFrame(Operation const & op, OperationResult & res, TransactionFrame & parentTx) :
	ReviewRequestOpFrame(op, res, parentTx)
{
}

}
