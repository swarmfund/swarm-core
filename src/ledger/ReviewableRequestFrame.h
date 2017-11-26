#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryFrame.h"
#include "ledger/LedgerManager.h"
#include <functional>
#include <unordered_map>

namespace soci
{
class session;
}

namespace stellar
{
class StatementContext;

class ReviewableRequestFrame : public EntryFrame
{

	static const char* select;

    static void
    loadRequests(StatementContext& prep,
               std::function<void(LedgerEntry const&)> requestsProcessor);

    ReviewableRequestEntry& mRequest;

    ReviewableRequestFrame(ReviewableRequestFrame const& from);

    void storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert);

	static bool isAssetCreateValid(AssetCreationRequest const & request);
	static bool isAssetUpdateValid(AssetUpdateRequest const& request);
	static bool isPreIssuanceValid(PreIssuanceRequest const& request);
	static bool isIssuanceValid(IssuanceRequest const& request);

  public:
    typedef std::shared_ptr<ReviewableRequestFrame> pointer;

    ReviewableRequestFrame();
    ReviewableRequestFrame(LedgerEntry const& from);

    ReviewableRequestFrame& operator=(ReviewableRequestFrame const& other);

	static pointer createNew(uint64 id, AccountID requestor, AccountID reviewer, xdr::pointer<stellar::string64> reference);
	// creates new reviewable request and calculates hash for it
	static pointer createNewWithHash(uint64 id, AccountID requestor, AccountID reviewer, xdr::pointer<stellar::string64> reference, ReviewableRequestEntry::_body_t body);

	void setBody(ReviewableRequestEntry::_body_t body) {
		mRequest.body = body;
	}

	AccountID getRequestor() const {
		return mRequest.requestor;
	}

	AccountID getReviewer() {
		return mRequest.reviewer;
	}

	xdr::pointer<stellar::string64> getReference() {
		return mRequest.reference;
	}

	uint64 getRequestID() const {
		return mRequest.ID;
	}

	stellar::string256 const& getRejectReason() const {
		return mRequest.rejectReason;
	}

	ReviewableRequestType getRequestType() const {
		return mRequest.body.type();
	}

	ReviewableRequestEntry const& getRequestEntry() const {
		return mRequest;
	}

	ReviewableRequestEntry& getRequestEntry() {
		return mRequest;
	}

	Hash const& getHash() {
		return mRequest.hash;
	}

	ReviewableRequestType getType() {
		return mRequest.body.type();
	}

	void setRejectReason(stellar::string256 rejectReason) {
		mRequest.rejectReason = rejectReason;
	}

	static uint256 calculateHash(ReviewableRequestEntry::_body_t const& body);

	void recalculateHashRejectReason() {
		auto newHash = calculateHash(mRequest.body);
		// nothing to update
		if (newHash == mRequest.hash)
			return;
		mRequest.hash = newHash;
		mRequest.rejectReason = "";
	}

    EntryFrame::pointer
    copy() const override
    {
        return EntryFrame::pointer(new ReviewableRequestFrame(*this));
    }
        
    static bool isValid(ReviewableRequestEntry const& oe);
    bool isValid() const;

    // Instance-based overrides of EntryFrame.
    void storeDelete(LedgerDelta& delta, Database& db) const override;
    void storeChange(LedgerDelta& delta, Database& db) override;
    void storeAdd(LedgerDelta& delta, Database& db) override;

	static void storeDelete(LedgerDelta& delta, Database& db, LedgerKey const& key);
	static bool exists(Database& db, LedgerKey const& key);
	static bool exists(Database& db, AccountID const& requestor, stellar::string64 reference);
    static uint64_t countObjects(soci::session& sess);
	// returns true if reviewable request with such reference already exist or reference already exists
	static bool isReferenceExist(Database& db, AccountID const& requestor, stellar::string64 reference);

    // database utilities
	// loadRequest - loads request by it's id. If not found returns nullptr.
    static pointer loadRequest(uint64 requestID, Database& db, LedgerDelta* delta = nullptr);
	// loadRequest - loads request by it's id and requestor, if not found returns nullptr.
	static pointer loadRequest(uint64 requestID, AccountID requestor, Database& db, LedgerDelta* delta = nullptr);
	// loadRequest - loads request by it's id, requestor and request type, if not found returns nullptr.
	static pointer loadRequest(uint64 requestID, AccountID requestor, ReviewableRequestType requestType, Database& db, LedgerDelta* delta = nullptr);

    static void dropAll(Database& db);

};
}
