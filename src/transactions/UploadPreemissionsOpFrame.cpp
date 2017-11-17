// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/asio.h"
#include "transactions/UploadPreemissionsOpFrame.h"
#include "transactions/SignatureValidator.h"
#include "ledger/CoinsEmissionRequestFrame.h"
#include "ledger/CoinsEmissionFrame.h"
#include "util/Logging.h"
#include "util/types.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"
#include "main/Application.h"
#include "crypto/SHA.h"

// convert from sheep to wheat
// selling sheep
// buying wheat

namespace stellar
{

using namespace std;
using xdr::operator==;

UploadPreemissionsOpFrame::UploadPreemissionsOpFrame(Operation const& op,
                                       OperationResult& res,
                                       TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mUploadPreemissions(mOperation.body.uploadPreemissionsOp())
{
}

bool
UploadPreemissionsOpFrame::doApply(Application& app,
                            LedgerDelta& delta, LedgerManager& ledgerManager)
{
    Database& db = ledgerManager.getDatabase();
    auto preEmissions = mUploadPreemissions.preEmissions;
    vector<shared_ptr<CoinsEmissionFrame>> emissionFrames;
    vector<string> serialNumbers;
    uint64 total = 0;
    AssetCode asset = preEmissions.begin()->asset;
    if (!AssetFrame::exists(db, asset))
    {
        app.getMetrics().NewMeter({ "op-upload-preemissions", "invalid", "asset-not-found" },
            "operation").Mark();
        innerResult().code(UPLOAD_PREEMISSIONS_ASSET_NOT_FOUND);
        return false;
    }
    for (auto preEm = preEmissions.begin() ; preEm != preEmissions.end(); ++preEm)
    {
        LedgerEntry le;
        le.data.type(COINS_EMISSION);
        CoinsEmissionEntry& emission = le.data.coinsEmission();
        emission.serialNumber = preEm->serialNumber;
        emission.amount = preEm->amount;
        emission.asset = preEm->asset;
        auto emissionFrame = std::make_shared<CoinsEmissionFrame>(le);
        emissionFrames.push_back(emissionFrame);
        serialNumbers.push_back(emission.serialNumber);
        total += preEm->amount;
    }
    if (CoinsEmissionFrame::exists(db, serialNumbers))
    {
        app.getMetrics().NewMeter({ "op-upload-preemissions", "invalid", "serial-duplication" },
            "operation").Mark();
        innerResult().code(UPLOAD_PREEMISSIONS_SERIAL_DUPLICATION);
        return false;
    }

    CoinsEmissionFrame::storePreEmissions(emissionFrames, delta, db);

    auto masterBalanceFrame = BalanceFrame::loadBalance(app.getMasterID(),
        asset,
        app.getDatabase(), &delta);
	assert(masterBalanceFrame);
	auto masetAccount = AccountFrame::loadAccount(delta, masterBalanceFrame->getAccountID(), db);
	assert(masetAccount);
    if (!masterBalanceFrame->addBalance(total))
    {
        app.getMetrics().NewMeter({ "op-upload-preemissions", "invalid", "line-full" },
            "operation").Mark();
        innerResult().code(UPLOAD_PREEMISSIONS_LINE_FULL);
        return false;
    }
    
    masterBalanceFrame->storeChange(delta, db);

	innerResult().code(UPLOAD_PREEMISSIONS_SUCCESS);
	app.getMetrics().NewMeter({ "op-upload-preemissions", "success", "apply" },
		"operation").Mark();
	return true;
}

// makes sure the currencies are different
bool
UploadPreemissionsOpFrame::doCheckValid(Application& app)
{
    if (mUploadPreemissions.preEmissions.size() == 0 ||
        mUploadPreemissions.preEmissions.size() > app.getConfig().PREEMISSIONS_PER_OP)
    {
        app.getMetrics().NewMeter({ "op-upload-preemissions", "invalid", "preemissions-malformed" },
			"operation").Mark();
		innerResult().code(UPLOAD_PREEMISSIONS_MALFORMED);
		return false;
    }

    auto preEmissions = mUploadPreemissions.preEmissions;
    auto asset = preEmissions.begin()->asset;
    if (!isAssetValid(asset))
    {
        app.getMetrics().NewMeter({"op-upload-preemissions", "invalid",
                        "malformed-invalid-asset"},
                        "operation").Mark();
        innerResult().code(UPLOAD_PREEMISSIONS_MALFORMED_PREEMISSIONS);
        return false;
    }

    for (auto preEm = preEmissions.begin(); preEm != preEmissions.end(); ++preEm)
    {
        if (preEm->asset != asset)
        {
            app.getMetrics().NewMeter({"op-upload-preemissions", "invalid",
                            "malformed-different-assets"},
                            "operation").Mark();
            innerResult().code(UPLOAD_PREEMISSIONS_MALFORMED_PREEMISSIONS);
            return false;
        }
        if (preEm->amount == 0)
        {
            app.getMetrics().NewMeter({ "op-upload-preemissions", "invalid", "malformed-zero-amount" },
                "operation").Mark();
            innerResult().code(UPLOAD_PREEMISSIONS_MALFORMED_PREEMISSIONS);
            return false;
        }

        if (preEm->amount % app.getConfig().EMISSION_UNIT != 0)
        {
            app.getMetrics().NewMeter({ "op-upload-preemissions", "invalid", "malformed-not-dividable-by-unit" },
                "operation").Mark();
            innerResult().code(UPLOAD_PREEMISSIONS_MALFORMED_PREEMISSIONS);
            return false;
        }
        
        if (preEm->serialNumber.size() == 0)
        {
            app.getMetrics().NewMeter({ "op-upload-preemissions", "invalid", "preemissions-malformed" },
                "operation").Mark();
            innerResult().code(UPLOAD_PREEMISSIONS_MALFORMED_PREEMISSIONS);
            return false;
        }
        if (preEm->signatures.size() == 0)
        {
            app.getMetrics().NewMeter({ "op-review-coins-emission-request", "invalid", "request-malformed" },
                "operation").Mark();
            innerResult().code(UPLOAD_PREEMISSIONS_MALFORMED_PREEMISSIONS);
            return false;
        }
        
        if (!validatePreemissionSignature(*preEm, app.getLedgerManager()))
        {
            app.getMetrics().NewMeter({ "op-review-coins-emission-request", "invalid", "preemission-malformed" },
                "operation").Mark();
            innerResult().code(UPLOAD_PREEMISSIONS_MALFORMED_PREEMISSIONS);
            return false;
        }
        
    }
    return true;
}

std::unordered_map<AccountID, CounterpartyDetails> UploadPreemissionsOpFrame::getCounterpartyDetails(Database & db, LedgerDelta * delta) const
{
	// no counterparties
	return{};
}

SourceDetails UploadPreemissionsOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const
{
	return SourceDetails({MASTER}, mSourceAccount->getHighThreshold(), SIGNER_EMISSION_MANAGER);
}

bool UploadPreemissionsOpFrame::validatePreemissionSignature(PreEmission preEm, LedgerManager& ledgerManager)
{
    auto data = preEm.serialNumber + ":" + std::to_string(preEm.amount) + ":" + preEm.asset;
    auto hash = Hash(sha256(data));
    auto signatureValidator = SignatureValidator(hash, preEm.signatures);

    SignatureValidator::Result result =
        signatureValidator.check(ledgerManager.getCurrentLedgerHeader().issuanceKeys, 1);
    return result == SignatureValidator::Result::SUCCESS;
}

}
