// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ReviewableRequestFrame.h"
#include "database/Database.h"
#include "LedgerDelta.h"
#include "ledger/ReferenceFrame.h"
#include "ledger/AssetFrame.h"
#include "xdrpp/printer.h"
#include "crypto/SHA.h"

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

ReviewableRequestFrame::pointer ReviewableRequestFrame::createNew(LedgerDelta& delta, AccountID requestor, AccountID reviewer, xdr::pointer<stellar::string64> reference)
{
	LedgerEntry entry;
	entry.data.type(LedgerEntryType::REVIEWABLE_REQUEST);
	auto& request = entry.data.reviewableRequest();
	request.requestor = requestor;
	request.reviewer = reviewer;
	request.requestID = delta.getHeaderFrame().generateID(LedgerEntryType::REVIEWABLE_REQUEST);
	request.reference = reference;
	return make_shared<ReviewableRequestFrame>(entry);
}

ReviewableRequestFrame::pointer ReviewableRequestFrame::createNewWithHash(LedgerDelta& delta, AccountID requestor, AccountID reviewer, xdr::pointer<stellar::string64> reference, ReviewableRequestEntry::_body_t body)
{
	auto result = createNew(delta, requestor, reviewer, reference);
	auto& reviewableRequestEntry = result->getRequestEntry();
	reviewableRequestEntry.body = body;
	result->recalculateHashRejectReason();
	return result;
}

bool ReviewableRequestFrame::isAssetCreateValid(AssetCreationRequest const& request)
{
	return AssetFrame::isAssetCodeValid(request.code) && request.name != "";
}

bool ReviewableRequestFrame::isAssetUpdateValid(AssetUpdateRequest const& request)
{
	return AssetFrame::isAssetCodeValid(request.code);
}

bool ReviewableRequestFrame::isPreIssuanceValid(PreIssuanceRequest const & request)
{
	return AssetFrame::isAssetCodeValid(request.asset) && request.amount != 0;
}

bool ReviewableRequestFrame::isIssuanceValid(IssuanceRequest const & request)
{
	return AssetFrame::isAssetCodeValid(request.asset) && request.amount != 0;
}

bool ReviewableRequestFrame::isWithdrawalValid(WithdrawalRequest const& request)
{
    auto isValid = true;
    switch (request.details.withdrawalType())
    {
    case WithdrawalType::AUTO_CONVERSION:
        {
        isValid = isValid && AssetFrame::isAssetCodeValid(request.details.autoConversion().destAsset) && request.details.autoConversion().expectedAmount > 0;
        }
    default: break;
    }
    return isValid && request.amount > 0;
}

uint256 ReviewableRequestFrame::calculateHash(ReviewableRequestEntry::_body_t const & body)
{
	return sha256(xdr::xdr_to_opaque(body));
}

bool
ReviewableRequestFrame::isValid(ReviewableRequestEntry const& oe)
{
	auto hash = calculateHash(oe.body);
	if (oe.hash != hash)
		return false;
	switch (oe.body.type()) {
	case ReviewableRequestType::ASSET_CREATE:
		return isAssetCreateValid(oe.body.assetCreationRequest());
	case ReviewableRequestType::ASSET_UPDATE:
		return isAssetUpdateValid(oe.body.assetUpdateRequest());
	case ReviewableRequestType::ISSUANCE_CREATE:
		return isIssuanceValid(oe.body.issuanceRequest());
	case ReviewableRequestType::PRE_ISSUANCE_CREATE:
		return isPreIssuanceValid(oe.body.preIssuanceRequest());
        case ReviewableRequestType::WITHDRAW:
            return isWithdrawalValid(oe.body.withdrawalRequest());            
	default:
		return false;
	}
}

bool
ReviewableRequestFrame::isValid() const
{
    return isValid(mRequest);
}
}

