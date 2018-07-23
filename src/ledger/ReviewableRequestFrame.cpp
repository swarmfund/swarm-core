// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <lib/json/json.h>
#include "ReviewableRequestFrame.h"
#include "database/Database.h"
#include "LedgerDelta.h"
#include "ledger/ReferenceFrame.h"
#include "ledger/AssetFrame.h"
#include "xdrpp/printer.h"
#include "crypto/SHA.h"
#include "SaleFrame.h"
#include "util/types.h"

using namespace soci;
using namespace std;

namespace stellar
{
using xdr::operator<;

ReviewableRequestFrame::ReviewableRequestFrame() : EntryFrame(LedgerEntryType::REVIEWABLE_REQUEST), mRequest(mEntry.data.reviewableRequest())
{
}

ReviewableRequestFrame::ReviewableRequestFrame(LedgerEntry const& from)
    : EntryFrame(from), mRequest(mEntry.data.reviewableRequest())
{
}

ReviewableRequestFrame::ReviewableRequestFrame(ReviewableRequestFrame const& from) : ReviewableRequestFrame(from.mEntry)
{
}

ReviewableRequestFrame& ReviewableRequestFrame::operator=(ReviewableRequestFrame const& other)
{
    if (&other != this)
    {
        mRequest = other.mRequest;
        mKey = other.mKey;
        mKeyCalculated = other.mKeyCalculated;
    }
    return *this;
}

ReviewableRequestFrame::pointer
ReviewableRequestFrame::createNew(LedgerDelta &delta, AccountID requestor, AccountID reviewer, xdr::pointer<stellar::string64> reference,
                                  time_t createdAt)
{
    return createNew(delta.getHeaderFrame().generateID(LedgerEntryType::REVIEWABLE_REQUEST), requestor, reviewer, reference, createdAt);
}

ReviewableRequestFrame::pointer ReviewableRequestFrame::createNew(uint64_t requestID, AccountID requestor, AccountID reviewer,
    xdr::pointer<string64> reference, time_t createdAt)
{
	LedgerEntry entry;
	entry.data.type(LedgerEntryType::REVIEWABLE_REQUEST);
	auto& request = entry.data.reviewableRequest();
	request.requestor = requestor;
	request.reviewer = reviewer;
	request.requestID = requestID;
	request.reference = reference;
        request.createdAt = createdAt;
	return make_shared<ReviewableRequestFrame>(entry);
}

ReviewableRequestFrame::pointer
ReviewableRequestFrame::createNewWithHash(LedgerDelta &delta, AccountID requestor, AccountID reviewer,
                                         xdr::pointer<stellar::string64> reference,
                                         ReviewableRequestEntry::_body_t body, time_t createdAt)
{
	auto result = createNew(delta, requestor, reviewer, reference, createdAt);
	auto& reviewableRequestEntry = result->getRequestEntry();
	reviewableRequestEntry.body = body;
	result->recalculateHashRejectReason();
	return result;
}

void ReviewableRequestFrame::ensureAssetCreateValid(AssetCreationRequest const& request)
{
    const auto owner = AccountID{};
    AssetFrame::create(request, owner)->ensureValid();
}

void ReviewableRequestFrame::ensureAssetUpdateValid(AssetUpdateRequest const& request)
{
	if (!AssetFrame::isAssetCodeValid(request.code))
	{
            throw runtime_error("Asset code is invalid");
	}

    if (!isValidJson(request.details))
    {
        throw runtime_error("invalid details");
    }
}

void ReviewableRequestFrame::ensurePreIssuanceValid(PreIssuanceRequest const & request)
{
    if (!AssetFrame::isAssetCodeValid(request.asset))
    {
        throw runtime_error("invalid asset code");
    }

    if (request.amount == 0)
    {
        throw runtime_error("invalid amount");
    }
}

void ReviewableRequestFrame::ensureIssuanceValid(IssuanceRequest const & request)
{
    if (!AssetFrame::isAssetCodeValid(request.asset))
    {
        throw runtime_error("invalid asset code");
    }

    if (request.amount == 0)
    {
        throw runtime_error("invalid amount");
    }

    if (!isValidJson(request.externalDetails))
    {
        throw runtime_error("invalid external details");
    }
}

void ReviewableRequestFrame::ensureWithdrawalValid(WithdrawalRequest const& request)
{
    if (request.amount == 0)
    {
        throw runtime_error("amount is invalid");
    }

    if (!isValidJson(request.externalDetails))
    {
        throw runtime_error("external details is invalid");
    }

    switch (request.details.withdrawalType())
    {
    case WithdrawalType::AUTO_CONVERSION:
        {
            if (!AssetFrame::isAssetCodeValid(request.details.autoConversion().destAsset))
            {
                throw runtime_error("dest asset is invalid");
            }
            
            if (request.details.autoConversion().expectedAmount == 0)
            {
                throw runtime_error("destination amount is invalid");
            }
        }
    default: break;
    }
}

void ReviewableRequestFrame::ensureSaleCreationValid(
    SaleCreationRequest const& request)
{
    const AccountID dummyAccountID;
    map<AssetCode, BalanceID> dummyBalances;
    const BalanceID dummyBalanceID;
    for (auto const& quoteAsset : request.quoteAssets)
    {
        dummyBalances[quoteAsset.quoteAsset] = dummyBalanceID;
    }
    const auto saleFrame = SaleFrame::createNew(0, dummyAccountID, request, dummyBalances, 0);
    saleFrame->ensureValid();
}
void ReviewableRequestFrame::ensureAMLAlertValid(AMLAlertRequest const &request) {
    if(request.reason.empty()){
        throw runtime_error("reason is invalid");
    }

    if (request.amount == 0)
    {
        throw runtime_error("amount can not be 0");
    }

}

void ReviewableRequestFrame::ensureUpdateKYCValid(UpdateKYCRequest const &request) {
	if (!isValidJson(request.kycData)) {
		throw std::runtime_error("KYC data is invalid");
	}
	bool res = isValidEnumValue(request.accountTypeToSet);
	if (!res) {
		throw runtime_error("invalid account type");
	}
}

void ReviewableRequestFrame::ensureUpdateSaleDetailsValid(UpdateSaleDetailsRequest const &request) {
    if (!isValidJson(request.newDetails)) {
        throw std::runtime_error("New sale details is invalid");
    }
}

uint256 ReviewableRequestFrame::calculateHash(ReviewableRequestEntry::_body_t const & body)
{
	return sha256(xdr::xdr_to_opaque(body));
}

void ReviewableRequestFrame::ensureValid(ReviewableRequestEntry const& oe)
{
    try
    {
        const auto hash = calculateHash(oe.body);
        if (oe.hash != hash)
            throw runtime_error("Calculated hash does not match one in request");
        switch (oe.body.type()) {
        case ReviewableRequestType::ASSET_CREATE:
            ensureAssetCreateValid(oe.body.assetCreationRequest());
            return;
        case ReviewableRequestType::ASSET_UPDATE:
            ensureAssetUpdateValid(oe.body.assetUpdateRequest());
            return;
        case ReviewableRequestType::ISSUANCE_CREATE:
            ensureIssuanceValid(oe.body.issuanceRequest());
            return;
        case ReviewableRequestType::PRE_ISSUANCE_CREATE:
            ensurePreIssuanceValid(oe.body.preIssuanceRequest());
            return;
        case ReviewableRequestType::WITHDRAW:
            ensureWithdrawalValid(oe.body.withdrawalRequest());
            return;
        case ReviewableRequestType::SALE:
            ensureSaleCreationValid(oe.body.saleCreationRequest());
            return;
        case ReviewableRequestType::LIMITS_UPDATE:
            return;
        case ReviewableRequestType::TWO_STEP_WITHDRAWAL:
            ensureWithdrawalValid(oe.body.twoStepWithdrawalRequest());
            return;
        case ReviewableRequestType::AML_ALERT:
            ensureAMLAlertValid(oe.body.amlAlertRequest());
            return;
		case ReviewableRequestType::UPDATE_KYC:
            ensureUpdateKYCValid(oe.body.updateKYCRequest());
			return;
        case ReviewableRequestType::UPDATE_SALE_DETAILS:
            ensureUpdateSaleDetailsValid(oe.body.updateSaleDetailsRequest());
            return;
        case ReviewableRequestType::UPDATE_SALE_END_TIME:
            return;
            case ReviewableRequestType ::UPDATE_PROMOTION:
            return;
        default:
            throw runtime_error("Unexpected reviewable request type");
        }
    } catch(exception ex)
    {
        CLOG(ERROR, Logging::ENTRY_LOGGER) << "Reviewable request is invalid: " << xdr::xdr_to_string(oe) << " reason:" << ex.what();
        throw_with_nested(runtime_error("Reviewable request is invalid")); 
    }
}

void
ReviewableRequestFrame::ensureValid() const
{
    ensureValid(mRequest);
}

void ReviewableRequestFrame::setTasks(uint32_t allTasks)
{
    mRequest.ext.v(LedgerVersion::ADD_TASKS_TO_REVIEWABLE_REQUEST);
    mRequest.ext.tasksExt().allTasks = allTasks;
    mRequest.ext.tasksExt().pendingTasks = allTasks;
}

}
