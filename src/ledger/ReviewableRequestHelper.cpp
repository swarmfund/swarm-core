//
// Created by kirill on 05.12.17.
//

#include "ReviewableRequestHelper.h"
#include "xdrpp/printer.h"
#include "LedgerDelta.h"
#include "util/basen.h"
#include "ReferenceFrame.h"

using namespace soci;
using namespace std;

namespace stellar {
    using xdr::operator<;

    const char* selectorReviewableRequest = "SELECT id, hash, body, requestor, reviewer, reference, reject_reason, version, lastmodified FROM reviewable_request";

    void ReviewableRequestHelper::dropAll(Database &db) {
        db.getSession() << "DROP TABLE IF EXISTS reviewable_request;";
        db.getSession() << "CREATE TABLE reviewable_request"
                "("
                "id            BIGINT        NOT NULL CHECK (id >= 0),"
                "hash          CHARACTER(64) NOT NULL,"
                "body          TEXT          NOT NULL,"
                "requestor     VARCHAR(56)   NOT NULL,"
                "reviewer      VARCHAR(56)   NOT NULL,"
                "reference     VARCHAR(64),"
                "reject_reason TEXT          NOT NULL,"
                "version       INT           NOT NULL,"
                "lastmodified  INT           NOT NULL,"
                "PRIMARY KEY (id)"
                ");";
        db.getSession() << "CREATE UNIQUE INDEX requestor_reference ON reviewable_request (requestor, reference) WHERE reference IS NOT NULL;";

    }

    void ReviewableRequestHelper::storeAdd(LedgerDelta &delta, Database &db, LedgerEntry const &entry) {
        storeUpdateHelper(delta, db, true, entry);
    }

    void ReviewableRequestHelper::storeChange(LedgerDelta &delta, Database &db, LedgerEntry const &entry) {
        storeUpdateHelper(delta, db, false, entry);
    }

    void ReviewableRequestHelper::storeDelete(LedgerDelta &delta, Database &db, LedgerKey const &key) {
        flushCachedEntry(key, db);
        auto timer = db.getDeleteTimer("reviewable_request");
        auto prep = db.getPreparedStatement("DELETE FROM reviewable_request WHERE id=:id");
        auto& st = prep.statement();
        st.exchange(use(key.reviewableRequest().requestID));
        st.define_and_bind();
        st.execute(true);
        delta.deleteEntry(key);
    }

    bool ReviewableRequestHelper::exists(Database &db, LedgerKey const &key) {
        if (cachedEntryExists(key, db)) {
            return true;
        }

        auto timer = db.getSelectTimer("reviewable_request_exists");
        auto prep =
                db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM reviewable_request WHERE id=:id)");
        auto& st = prep.statement();
        st.exchange(use(key.reviewableRequest().requestID));
        int exists = 0;
        st.exchange(into(exists));
        st.define_and_bind();
        st.execute(true);

        return exists != 0;
    }

    LedgerKey ReviewableRequestHelper::getLedgerKey(LedgerEntry const &from) {
        return LedgerKey();
    }

    EntryFrame::pointer ReviewableRequestHelper::storeLoad(LedgerKey const &key, Database &db) {
        return stellar::EntryFrame::pointer();
    }

    EntryFrame::pointer ReviewableRequestHelper::fromXDR(LedgerEntry const &from) {
        return stellar::EntryFrame::pointer();
    }

    uint64_t ReviewableRequestHelper::countObjects(soci::session &sess) {
        uint64_t count = 0;
        sess << "SELECT COUNT(*) FROM reviewable_request;", into(count);
        return count;
    }

    void ReviewableRequestHelper::storeUpdateHelper(LedgerDelta &delta, Database &db, bool insert,
                                                    const LedgerEntry &entry) {
        auto reviewableRequest = make_shared<ReviewableRequestFrame>(entry);

        reviewableRequest->touch(delta);

        if (!reviewableRequest->isValid())
        {
            CLOG(ERROR, Logging::ENTRY_LOGGER)
                    << "Unexpected state - request is invalid: "
                    << xdr::xdr_to_string(reviewableRequest->getRequestEntry());
            throw std::runtime_error("Unexpected state - reviewable request is invalid");
        }

        auto key = reviewableRequest->getKey();
        flushCachedEntry(key, db);
        string sql;

        std::string hash = binToHex(reviewableRequest->getHash());
        auto bodyBytes = xdr::xdr_to_opaque(reviewableRequest->getRequestEntry().body);
        std::string strBody = bn::encode_b64(bodyBytes);
        std::string requestor = PubKeyUtils::toStrKey(reviewableRequest->getRequestor());
        std::string rejectReason = reviewableRequest->getRejectReason();
        auto version = static_cast<int32_t>(reviewableRequest->getRequestEntry().ext.v());

        if (insert)
        {
            sql = "INSERT INTO reviewable_request (id, hash, body, requestor, reviewer, reference, reject_reason, version, lastmodified)"
                    " VALUES (:id, :hash, :body, :requestor, :reviewer, :reference, :reject_reason, :v, :lm)";
        }
        else
        {
            sql = "UPDATE reviewable_request SET hash=:hash, body = :body, requestor = :requestor, reviewer = :reviewer, reference = :reference, reject_reason = :reject_reason, version=:v, lastmodified=:lm"
                    " WHERE id = :id";
        }

        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();

        st.exchange(use(reviewableRequest->getRequestID(), "id"));
        st.exchange(use(hash, "hash"));
        st.exchange(use(strBody, "body"));
        st.exchange(use(reviewableRequest->getRequestor(), "requestor"));
        st.exchange(use(reviewableRequest->getReviewer(), "reviewer"));
        st.exchange(use(reviewableRequest->getReference(), "reference"));
        st.exchange(use(rejectReason, "reject_reason"));
        st.exchange(use(version, "v"));
        st.exchange(use(reviewableRequest->getLastModified(), "lm"));
        st.define_and_bind();

        auto timer = insert ? db.getInsertTimer("reviewable_request") : db.getUpdateTimer("reviewable_request");
        st.execute(true);

        if (st.get_affected_rows() != 1)
        {
            throw std::runtime_error("could not update SQL");
        }

        if (insert)
        {
            delta.addEntry(*reviewableRequest);
        }
        else
        {
            delta.modEntry(*reviewableRequest);
        }
    }

    void ReviewableRequestHelper::loadRequests(StatementContext &prep,
                                               std::function<void(LedgerEntry const &)> requestsProcessor) {
        LedgerEntry le;
        le.data.type(LedgerEntryType::REVIEWABLE_REQUEST);
        ReviewableRequestEntry& oe = le.data.reviewableRequest();
        std::string hash, body, rejectReason;
        int version;

        statement& st = prep.statement();
        st.exchange(into(oe.requestID));
        st.exchange(into(hash));
        st.exchange(into(body));
        st.exchange(into(oe.requestor));
        st.exchange(into(oe.reviewer));
        st.exchange(into(oe.reference));
        st.exchange(into(rejectReason));
        st.exchange(into(version));
        st.exchange(into(le.lastModifiedLedgerSeq));
        st.define_and_bind();
        st.execute(true);

        while (st.got_data())
        {
            oe.hash = hexToBin256(hash);

            // unmarshal body
            std::vector<uint8_t> decoded;
            bn::decode_b64(body, decoded);
            xdr::xdr_get unmarshaler(&decoded.front(), &decoded.back() + 1);
            xdr::xdr_argpack_archive(unmarshaler, oe.body);
            unmarshaler.done();

            oe.rejectReason = rejectReason;
            oe.ext.v((LedgerVersion)version);
            if (!ReviewableRequestFrame::isValid(oe))
            {
                CLOG(ERROR, Logging::ENTRY_LOGGER) << "Unexpected state: invalid reviewable request: " << xdr::xdr_to_string(oe);
                throw std::runtime_error("Invalid reviewable request");
            }

            requestsProcessor(le);
            st.fetch();
        }
    }

    bool ReviewableRequestHelper::exists(Database &db, AccountID const &requestor, stellar::string64 reference) {
        auto timer = db.getSelectTimer("reviewable_request_exists_by_reference");
        auto prep =
                db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM reviewable_request WHERE requestor=:requestor AND reference = :reference)");
        auto& st = prep.statement();
        st.exchange(use(requestor, "requestor"));
        st.exchange(use(reference, "reference"));
        int exists = 0;
        st.exchange(into(exists));
        st.define_and_bind();
        st.execute(true);

        return exists != 0;
    }

    bool
    ReviewableRequestHelper::isReferenceExist(Database &db, AccountID const &requestor, stellar::string64 reference) {
        if (exists(db, requestor, reference))
            return true;
        //TODO: rewrite to helper
        return ReferenceFrame::exists(db, reference, requestor);
    }

    ReviewableRequestFrame::pointer
    ReviewableRequestHelper::loadRequest(uint64 requestID, Database &db, LedgerDelta *delta) {
        LedgerKey key;
        key.type(LedgerEntryType::REVIEWABLE_REQUEST);
        key.reviewableRequest().requestID = requestID;
        if (cachedEntryExists(key, db))
        {
            auto p = getCachedEntry(key, db);
            return p ? std::make_shared<ReviewableRequestFrame>(*p) : nullptr;
        }

        std::string sql = selectorReviewableRequest;
        sql += +" WHERE id = :id";
        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();
        st.exchange(use(requestID));

        ReviewableRequestFrame::pointer retReviewableRequest;
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

        auto pEntry = std::make_shared<LedgerEntry const>(retReviewableRequest->mEntry);
        putCachedEntry(key, pEntry, db);
        return retReviewableRequest;
    }

    ReviewableRequestFrame::pointer
    ReviewableRequestHelper::loadRequest(uint64 requestID, AccountID requestor, ReviewableRequestType requestType,
                                         Database &db, LedgerDelta *delta) {
        auto request = loadRequest(requestID, requestor, db, delta);
        if (!request) {
            return nullptr;
        }

        if (request->getRequestEntry().body.type() == requestType)
            return request;

        return nullptr;
    }

    ReviewableRequestFrame::pointer
    ReviewableRequestHelper::loadRequest(uint64 requestID, AccountID requestor, Database &db, LedgerDelta *delta) {
        auto request = loadRequest(requestID, db, delta);
        if (!request) {
            return nullptr;
        }

        if (request->getRequestor() == requestor)
            return request;

        return nullptr;
    }
}