// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ReviewableRequestFrame.h"
#include "crypto/SecretKey.h"
#include "crypto/Hex.h"
#include "database/Database.h"
#include "LedgerDelta.h"
#include "ledger/LedgerManager.h"
#include "util/basen.h"
#include "util/types.h"
#include "lib/util/format.h"
#include <algorithm>
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

ReviewableRequestFrame::pointer ReviewableRequestFrame::createNew(uint64 id, AccountID requestor)
{
	LedgerEntry entry;
	entry.data.type(LedgerEntryType::REVIEWABLE_REQUEST);
	auto& request = entry.data.reviewableRequest();
	request.requestor = requestor;
	request.ID = id;
	return make_shared<ReviewableRequestFrame>(entry);
}

bool ReviewableRequestFrame::isAssetCreateValid(AssetCreationRequest const& request)
{
	return request.code != "" && request.name != "";
}

bool ReviewableRequestFrame::isAssetUpdateValid(AssetUpdateRequest const& request)
{
	return request.code != "";
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
	default:
		return false;
	}
}

bool
ReviewableRequestFrame::isValid() const
{
    return isValid(mRequest);
}

void ReviewableRequestFrame::storeUpdateHelper(LedgerDelta & delta, Database & db, bool insert)
{
	touch(delta);

	if (!isValid())
	{
		CLOG(ERROR, Logging::ENTRY_LOGGER) << "Unexpected state - request is invalid: " << xdr::xdr_to_string(mRequest);
		throw std::runtime_error("Unexpected state - reviewable request is invalid");
	}

	flushCachedEntry(db);
	string sql;

	std::string hash = binToHex(mRequest.hash);
	auto bodyBytes = xdr::xdr_to_opaque(mRequest.body);
	std::string strBody = bn::encode_b64(bodyBytes);
	std::string requestor = PubKeyUtils::toStrKey(mRequest.requestor);
	std::string rejectReason = mRequest.rejectReason;
	int32_t version = mRequest.ext.v();

	if (insert)
	{
		sql = "INSERT INTO reviewable_request (id, hash, body, requestor, reject_reason, version, lastmodified)"
			" VALUES (:id, :hash, :body, :requestor, :reject_reason, :v, :lm)";
	}
	else
	{
		sql = "UPDATE reviewable_request SET hash=:hash, body = :body, requestor = :requestor, reject_reason = :reject_reason, version=:v, lastmodified=:lm"
			" WHERE id = :id";
	}

	auto prep = db.getPreparedStatement(sql);
	auto& st = prep.statement();

	st.exchange(use(mRequest.ID, "id"));
	st.exchange(use(hash, "hash"));
	st.exchange(use(strBody, "body"));
	st.exchange(use(mRequest.requestor, "requestor"));
	st.exchange(use(rejectReason, "reject_reason"));
	st.exchange(use(version, "v"));
	st.exchange(use(mEntry.lastModifiedLedgerSeq, "lm"));
	st.define_and_bind();

	auto timer = insert ? db.getInsertTimer("reviewable_request") : db.getUpdateTimer("reviewable_request");
	st.execute(true);

	if (st.get_affected_rows() != 1)
	{
		throw std::runtime_error("could not update SQL");
	}

	if (insert)
	{
		delta.addEntry(*this);
	}
	else
	{
		delta.modEntry(*this);
	}
}

void ReviewableRequestFrame::storeDelete(LedgerDelta & delta, Database & db, LedgerKey const & key)
{
	flushCachedEntry(key, db);
	auto timer = db.getDeleteTimer("reviewable_request");
	auto prep = db.getPreparedStatement("DELETE FROM reviewable_request WHERE id=:id");
	auto& st = prep.statement();
	st.exchange(use(key.reviewableRequest().ID));
	st.define_and_bind();
	st.execute(true);
	delta.deleteEntry(key);
}

void ReviewableRequestFrame::storeDelete(LedgerDelta & delta, Database & db) const
{
	storeDelete(delta, db, getKey());
}

void ReviewableRequestFrame::storeChange(LedgerDelta & delta, Database & db)
{
	storeUpdateHelper(delta, db, false);
}

void ReviewableRequestFrame::storeAdd(LedgerDelta & delta, Database & db)
{
	storeUpdateHelper(delta, db, true);
}

void ReviewableRequestFrame::loadRequests(StatementContext & prep, std::function<void(LedgerEntry const&)> requestsProcessor)
{
	LedgerEntry le;
	le.data.type(REVIEWABLE_REQUEST);
	ReviewableRequestEntry& oe = le.data.reviewableRequest();
	std::string hash, requestor, body, rejectReason;
	int version;

	statement& st = prep.statement();
	st.exchange(into(oe.ID));
	st.exchange(into(hash));
	st.exchange(into(body));
	st.exchange(into(requestor));
	st.exchange(into(rejectReason));
	st.exchange(into(version));
	st.exchange(into(le.lastModifiedLedgerSeq));
	st.define_and_bind();
	st.execute(true);

	while (st.got_data())
	{
		oe.hash = hexToBin256(hash);

		// unmarhsal body
		std::vector<uint8_t> decoded;
		bn::decode_b64(body, decoded);
		xdr::xdr_get unmarshaler(&decoded.front(), &decoded.back() + 1);
		xdr::xdr_argpack_archive(unmarshaler, oe.body);
		unmarshaler.done();

		oe.requestor = PubKeyUtils::fromStrKey(requestor);
		oe.rejectReason = rejectReason;
		oe.ext.v((LedgerVersion)version);
		if (!isValid(oe))
		{
			CLOG(ERROR, Logging::ENTRY_LOGGER) << "Unexpected state: invalid reviewable request: " << xdr::xdr_to_string(oe);
			throw std::runtime_error("Invalid reviewable request");
		}

		requestsProcessor(le);
		st.fetch();
	}
}

bool ReviewableRequestFrame::exists(Database & db, LedgerKey const & key)
{
	if (cachedEntryExists(key, db)) {
		return true;
	}

	auto timer = db.getSelectTimer("reviewable_request_exists");
	auto prep =
		db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM reviewable_request WHERE id=:id)");
	auto& st = prep.statement();
	st.exchange(use(key.reviewableRequest().ID));
	int exists = 0;
	st.exchange(into(exists));
	st.define_and_bind();
	st.execute(true);

	return exists != 0;
}

uint64_t ReviewableRequestFrame::countObjects(soci::session & sess)
{
	uint64_t count = 0;
	sess << "SELECT COUNT(*) FROM reviewable_request;", into(count);
	return count;
}

ReviewableRequestFrame::pointer ReviewableRequestFrame::loadRequest(uint64 requestID, Database & db, LedgerDelta * delta)
{
	LedgerKey key;
	key.type(REVIEWABLE_REQUEST);
	key.reviewableRequest().ID = requestID;
	if (cachedEntryExists(key, db))
	{
		auto p = getCachedEntry(key, db);
		return p ? std::make_shared<ReviewableRequestFrame>(*p) : nullptr;
	}

	std::string sql = select;
	sql += +" WHERE id = :id";
	auto prep = db.getPreparedStatement(sql);
	auto& st = prep.statement();
	st.exchange(use(requestID));

	pointer retReviewableRequest;
	auto timer = db.getSelectTimer("reviewable_request");
	loadRequests(prep, [&retReviewableRequest](LedgerEntry const& entry)
	{
		retReviewableRequest = make_shared<ReviewableRequestFrame>(entry);
	});

	if (!retReviewableRequest)
	{
		putCachedEntry(key, nullptr, db);
		return nullptr;
	}

	if (delta)
	{
		delta->recordEntry(*retReviewableRequest);
	}

	retReviewableRequest->putCachedEntry(db);
	return retReviewableRequest;
}

ReviewableRequestFrame::pointer ReviewableRequestFrame::loadRequest(uint64 requestID, AccountID requestor, Database & db, LedgerDelta * delta)
{
	auto request = loadRequest(requestID, db, delta);
	if (!request) {
		return nullptr;
	}

	if (request->getRequestor() == requestor)
		return request;

	return nullptr;
}

ReviewableRequestFrame::pointer ReviewableRequestFrame::loadRequest(uint64 requestID, AccountID requestor, ReviewableRequestType requestType, Database & db, LedgerDelta * delta)
{
	auto request = loadRequest(requestID, requestor, db, delta);
	if (!request) {
		return nullptr;
	}

	if (request->getRequestEntry().body.type() == requestType)
		return request;

	return nullptr;
}

const char* ReviewableRequestFrame::select = "SELECT id, hash, body, requestor, reject_reason, version, lastmodified FROM reviewable_request";

void ReviewableRequestFrame::dropAll(Database & db)
{
	db.getSession() << "DROP TABLE IF EXISTS reviewable_request;";
	db.getSession() << "CREATE TABLE reviewable_request"
    "("
    "id            BIGINT        NOT NULL CHECK (id >= 0),"
	"hash          CHARACTER(64) NOT NULL,"
	"body          TEXT          NOT NULL,"
    "requestor     VARCHAR(56)   NOT NULL,"
	"reject_reason TEXT          NOT NULL,"
	"version       INT           NOT NULL,"
    "lastmodified  INT           NOT NULL,"
	"PRIMARY KEY (id)"
		");";
}
}

