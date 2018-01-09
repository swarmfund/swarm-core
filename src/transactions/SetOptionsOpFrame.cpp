// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <ledger/ReviewableRequestHelper.h>
#include <lib/json/json.h>
#include "transactions/SetOptionsOpFrame.h"
#include "ledger/TrustFrame.h"
#include "ledger/TrustHelper.h"
#include "ledger/BalanceHelper.h"
#include "database/Database.h"
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"
#include "ManageAccountOpFrame.h"

namespace stellar
{
using xdr::operator==;

std::unordered_map<AccountID, CounterpartyDetails> SetOptionsOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
{
	// no counterparties
	return{};
}

SourceDetails SetOptionsOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const
{
	return SourceDetails({AccountType::MASTER, AccountType::GENERAL, AccountType::NOT_VERIFIED, AccountType::SYNDICATE, AccountType::EXCHANGE},
                         mSourceAccount->getHighThreshold(),
                         static_cast<int32_t>(SignerType::ACCOUNT_MANAGER), static_cast<int32_t>(BlockReasons::KYC_UPDATE));
}

SetOptionsOpFrame::SetOptionsOpFrame(Operation const& op, OperationResult& res,
                                     TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mSetOptions(mOperation.body.setOptionsOp())
{
}

bool SetOptionsOpFrame::tryUpdateSigners(Application& app, LedgerManager& ledgerManager)
{
	if (!mSetOptions.signer)
		return true;

	int32_t signerVersion = static_cast<int32_t>(mSetOptions.signer->ext.v());
	if (ledgerManager.getCurrentLedgerHeader().ledgerVersion < signerVersion)
	{
		app.getMetrics().NewMeter({ "op-set-options", "failure",
									"invalid_signer_version" },
									"operation").Mark();
		innerResult().code(SetOptionsResultCode::INVALID_SIGNER_VERSION);
		return false;
	}

	AccountEntry& account = mSourceAccount->getAccount();
	auto& signers = account.signers;

	// delete
	if (mSetOptions.signer->weight <= 0)
	{
		auto it = signers.begin();
		while (it != signers.end())
		{
			Signer& oldSigner = *it;
			if (oldSigner.pubKey == mSetOptions.signer->pubKey)
			{
				it = signers.erase(it);
				continue;
			}
			
			it++;
		}

		mSourceAccount->setUpdateSigners(true);
		return true;
	}


	
	// update
	for (size_t i = 0; i < signers.size(); i++)
	{
		if (signers[i].pubKey == mSetOptions.signer->pubKey)
		{
			signers[i] = *mSetOptions.signer;
			mSourceAccount->setUpdateSigners(true);
			return true;
		}
	}

	// add new
	const size_t MAX_SIGNERS = 30;
	if (signers.size() == MAX_SIGNERS && !(getSourceID() == app.getMasterID()))
	{
		app.getMetrics().NewMeter({ "op-set-options", "failure",
			"too-many-signers" },
			"operation").Mark();
		innerResult().code(SetOptionsResultCode::TOO_MANY_SIGNERS);
		return false;
	}
	signers.push_back(*mSetOptions.signer);

	mSourceAccount->setUpdateSigners(true);
	return true;
}

void SetOptionsOpFrame::updateThresholds()
{
    AccountEntry& account = mSourceAccount->getAccount();

    if (mSetOptions.masterWeight)
    {
        account.thresholds[static_cast<int32_t>(ThresholdIndexes::MASTER_WEIGHT)] =
                *mSetOptions.masterWeight & UINT8_MAX;
    }

    if (mSetOptions.lowThreshold)
    {
        account.thresholds[static_cast<int32_t>(ThresholdIndexes::LOW)] =
                *mSetOptions.lowThreshold & UINT8_MAX;
    }

    if (mSetOptions.medThreshold)
    {
        account.thresholds[static_cast<int32_t>(ThresholdIndexes::MED)] =
                *mSetOptions.medThreshold & UINT8_MAX;
    }

    if (mSetOptions.highThreshold)
    {
        account.thresholds[static_cast<int32_t>(ThresholdIndexes::HIGH)] =
                *mSetOptions.highThreshold & UINT8_MAX;
    }

}

bool SetOptionsOpFrame::processTrustData(Application &app, LedgerDelta &delta)
{
    Database& db = app.getDatabase();
    auto trust = mSetOptions.trustData->trust;
    auto balanceHelper = BalanceHelper::Instance();
    auto balanceFrame = balanceHelper->loadBalance(trust.balanceToUse, db);
    if (!balanceFrame || !(balanceFrame->getAccountID() == getSourceID()))
    {
        app.getMetrics().NewMeter({"op-set-options", "failure",
                                   "balance-not-found"},
                                  "operation").Mark();
        innerResult().code(SetOptionsResultCode::BALANCE_NOT_FOUND);
        return false;
    }

    auto trustHelper = TrustHelper::Instance();

    if (mSetOptions.trustData->action == ManageTrustAction::TRUST_ADD)
    {
        auto trustLines = trustHelper->countForBalance(db, trust.balanceToUse);
        // TODO move to config
        if (trustLines > 20)
        {
            app.getMetrics().NewMeter({ "op-set-options", "failure",
                                        "too-many-trust-lines" },
                                      "operation").Mark();
            innerResult().code(SetOptionsResultCode::TRUST_TOO_MANY);
            return false;
        }

        auto trustFrame = TrustFrame::createNew(
                trust.allowedAccount, trust.balanceToUse);
        EntryHelperProvider::storeAddEntry(delta, db, trustFrame->mEntry);
    }
    else if (mSetOptions.trustData->action == ManageTrustAction::TRUST_REMOVE)
    {
        auto trustFrame = trustHelper->loadTrust(trust.allowedAccount,
                                                 trust.balanceToUse, db);
        EntryHelperProvider::storeDeleteEntry(delta, db, trustFrame->getKey());
    }
    return true;
}

ReviewableRequestFrame::pointer
SetOptionsOpFrame::getUpdatedOrCreateReviewableRequest(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager)
{
    if (!mSetOptions.updateKYCData)
        throw std::runtime_error("Unexpected state. Update KYC data is not set.");

    uint64_t requestID = mSetOptions.updateKYCData->requestID;
    auto updateKYCRequest = getOrCreateReviewableRequest(app, delta, ledgerManager);
    if (!updateKYCRequest)
        return nullptr;

    auto& updateKYCEntry = updateKYCRequest->getRequestEntry();
    updateKYCEntry.body.type(ReviewableRequestType::UPDATE_KYC);
    updateKYCEntry.body.updateKYCRequest().dataKYC = mSetOptions.updateKYCData->dataKYC;
    updateKYCEntry.body.updateKYCRequest().accountType = mSourceAccount->getAccountType();
    updateKYCRequest->recalculateHashRejectReason();

    return updateKYCRequest;
}

ReviewableRequestFrame::pointer SetOptionsOpFrame::getOrCreateReviewableRequest(Application &app, LedgerDelta &delta,
                                                LedgerManager &ledgerManager)
{
    if (!mSetOptions.updateKYCData)
        throw std::runtime_error("Unexpected state. Update KYC data is not set.");

    uint64_t requestID = mSetOptions.updateKYCData->requestID;
    if (requestID != 0)
    {
        auto updateKYCRequest = ReviewableRequestHelper::Instance()->loadRequest(requestID, ledgerManager.getDatabase());
        if (!updateKYCRequest)
            return nullptr;
        return updateKYCRequest;
    }

    auto reference = xdr::pointer<string64>(new string64(updateKYCReference));
    auto updateKYCRequest = ReviewableRequestFrame::createNew(delta, mSourceAccount->getID(), app.getMasterID(),
                                                              reference, ledgerManager.getCloseTime());
    return updateKYCRequest;
}

bool SetOptionsOpFrame::updateKYC(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager,
                                  uint64_t &requestID)
{
    Database& db = ledgerManager.getDatabase();
    // attempt to add another request
    if (mSetOptions.updateKYCData->requestID == 0 &&
            ReviewableRequestHelper::Instance()->exists(db, mSourceAccount->getID(), updateKYCReference))
    {
        app.getMetrics().NewMeter({"op-set-options", "failure",
                                   "update-KYC-request-malformed"},
                                  "operation").Mark();
        innerResult().code(SetOptionsResultCode::UPDATE_KYC_REQUEST_NOT_FOUND);
        return false;
    }

    auto updateKYCRequest = getUpdatedOrCreateReviewableRequest(app, delta, ledgerManager);
    if (!updateKYCRequest)
    {
        app.getMetrics().NewMeter({"op-set-options", "failure",
                                   "update-KYC-request-not-found"},
                                  "operation").Mark();
        innerResult().code(SetOptionsResultCode::UPDATE_KYC_REQUEST_NOT_FOUND);
        return false;
    }

    EntryHelperProvider::storeAddOrChangeEntry(delta, db, updateKYCRequest->mEntry);
    requestID = updateKYCRequest->getRequestID();

    mSourceAccount->setAccountType(AccountType::NOT_VERIFIED);

    return true;
}

bool
SetOptionsOpFrame::doApply(Application& app, LedgerDelta& delta,
                           LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();

    updateThresholds();

	if (!tryUpdateSigners(app, ledgerManager))
		return false;

    if (mSetOptions.trustData)
    {
        if (!processTrustData(app, delta))
            return false;
    }

    uint64_t requestID = 0;
    if (mSetOptions.updateKYCData)
    {
        if (!updateKYC(app, delta, ledgerManager, requestID))
            return false;
    }

    app.getMetrics().NewMeter({"op-set-options", "success", "apply"}, "operation")
        .Mark();
    innerResult().code(SetOptionsResultCode::SUCCESS);
    innerResult().success().requestID = requestID;
    EntryHelperProvider::storeChangeEntry(delta, db, mSourceAccount->mEntry);
    return true;
}

bool
SetOptionsOpFrame::doCheckValid(Application& app)
{
    if (mSetOptions.masterWeight)
    {
        if (*mSetOptions.masterWeight > UINT8_MAX)
        {
            app.getMetrics().NewMeter(
                        {"op-set-options", "invalid", "threshold-out-of-range"},
                        "operation").Mark();
            innerResult().code(SetOptionsResultCode::THRESHOLD_OUT_OF_RANGE);
            return false;
        }
    }

    if (mSetOptions.lowThreshold)
    {
        if (*mSetOptions.lowThreshold > UINT8_MAX)
        {
            app.getMetrics().NewMeter(
                        {"op-set-options", "invalid", "threshold-out-of-range"},
                        "operation").Mark();
            innerResult().code(SetOptionsResultCode::THRESHOLD_OUT_OF_RANGE);
            return false;
        }
    }

    if (mSetOptions.medThreshold)
    {
        if (*mSetOptions.medThreshold > UINT8_MAX)
        {
            app.getMetrics().NewMeter(
                        {"op-set-options", "invalid", "threshold-out-of-range"},
                        "operation").Mark();
            innerResult().code(SetOptionsResultCode::THRESHOLD_OUT_OF_RANGE);
            return false;
        }
    }

    if (mSetOptions.highThreshold)
    {
        if (*mSetOptions.highThreshold > UINT8_MAX)
        {
            app.getMetrics().NewMeter(
                        {"op-set-options", "invalid", "threshold-out-of-range"},
                        "operation").Mark();
            innerResult().code(SetOptionsResultCode::THRESHOLD_OUT_OF_RANGE);
            return false;
        }
    }

    if (mSetOptions.signer)
    {
        if (mSetOptions.signer->pubKey == getSourceID())
        {
            app.getMetrics().NewMeter({"op-set-options", "invalid", "bad-signer"},
                             "operation").Mark();
            innerResult().code(SetOptionsResultCode::BAD_SIGNER);
            return false;
        }
    }

    if (mSetOptions.trustData)
    {
        if (mSetOptions.trustData->trust.allowedAccount == getSourceID())
        {
            app.getMetrics().NewMeter({"op-set-options", "invalid", "bad-Trust-account"},
                             "operation").Mark();
            innerResult().code(SetOptionsResultCode::TRUST_MALFORMED);
            return false;
        }
    }

    if (mSetOptions.updateKYCData)
    {
        Json::Reader reader;
        Json::Value value;
        if (!reader.parse(mSetOptions.updateKYCData->dataKYC, value, false))
        {
            app.getMetrics().NewMeter({"op-set-options", "invalid", "KYC-data-malformed"},
                                      "operation").Mark();
            innerResult().code(SetOptionsResultCode::UPDATE_KYC_MALFORMED);
            return false;
        }
    }

    return true;
}

}
