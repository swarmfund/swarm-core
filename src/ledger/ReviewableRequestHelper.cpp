#include "ReviewableRequestHelper.h"
#include "xdrpp/marshal.h"
#include "xdrpp/printer.h"
#include "LedgerDelta.h"
#include "util/basen.h"
#include "ReferenceFrame.h"

using namespace soci;
using namespace std;

namespace stellar {
    using xdr::operator<;

    const char* selectorReviewableRequest = "SELECT id, hash, body, requestor, reviewer, reference, "
                                            "reject_reason, created_at, version, lastmodified, "
                                            "all_tasks, pending_tasks, external_details FROM reviewable_request";

    void ReviewableRequestHelper::addTasks(Database &db)
    {
        db.getSession() << "ALTER TABLE reviewable_request ADD all_tasks INT DEFAULT 0";
        db.getSession() << "ALTER TABLE reviewable_request ADD pending_tasks INT DEFAULT 0";
        db.getSession() << "ALTER TABLE reviewable_request ADD external_details TEXT";
    }

    void ReviewableRequestHelper::changeDefaultExternalDetails(Database &db)
    {
        db.getSession() << "ALTER TABLE reviewable_request ALTER COLUMN external_details SET DEFAULT ''";
    }

    void ReviewableRequestHelper::setEmptyStringToExternalDetailsInsteadNull(Database &db)
    {
        db.getSession() << "UPDATE reviewable_request SET external_details = '' where external_details is null";
    }

    void ReviewableRequestHelper::dropAll(Database &db) {
        db.getSession() << "DROP TABLE IF EXISTS reviewable_request CASCADE;";
        db.getSession() << "CREATE TABLE reviewable_request"
                "("
                "id            BIGINT        NOT NULL CHECK (id >= 0),"
                "hash          CHARACTER(64) NOT NULL,"
                "body          TEXT          NOT NULL,"
                "requestor     VARCHAR(56)   NOT NULL,"
                "reviewer      VARCHAR(56)   NOT NULL,"
                "reference     VARCHAR(64),"
                "reject_reason TEXT          NOT NULL,"
                "created_at    BIGINT        NOT NULL,"
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
        LedgerKey ledgerKey;
        ledgerKey.type(from.data.type());
        ledgerKey.reviewableRequest().requestID = from.data.reviewableRequest().requestID;
        return ledgerKey;
    }

    EntryFrame::pointer ReviewableRequestHelper::fromXDR(LedgerEntry const &from) {
        return std::make_shared<ReviewableRequestFrame>(from);
    }

    uint64_t ReviewableRequestHelper::countObjects(soci::session &sess) {
        uint64_t count = 0;
        sess << "SELECT COUNT(*) FROM reviewable_request;", into(count);
        return count;
    }

    void ReviewableRequestHelper::storeUpdateHelper(LedgerDelta &delta, Database &db, bool insert,
                                                    const LedgerEntry &entry) 
	{
        auto reviewableRequestFrame = make_shared<ReviewableRequestFrame>(entry);
		auto reviewableRequestEntry = reviewableRequestFrame->getRequestEntry();

        reviewableRequestFrame->touch(delta);

        reviewableRequestFrame->ensureValid();

        auto key = reviewableRequestFrame->getKey();
        flushCachedEntry(key, db);
        string sql;

        std::string hash = binToHex(reviewableRequestFrame->getHash());
        auto bodyBytes = xdr::xdr_to_opaque(reviewableRequestFrame->getRequestEntry().body);
        std::string strBody = bn::encode_b64(bodyBytes);
        std::string rejectReason = reviewableRequestFrame->getRejectReason();
        auto version = static_cast<int32_t>(reviewableRequestFrame->getRequestEntry().ext.v());

        uint32_t allTasks = reviewableRequestFrame->getAllTasks();
        uint32_t pendingTasks = reviewableRequestFrame->getPendingTasks();
        auto externalDetailsBytes = xdr::xdr_to_opaque(reviewableRequestFrame->getExternalDetails());
        auto strExternalDetails = bn::encode_b64(externalDetailsBytes);

        if (insert)
        {
            sql = "INSERT INTO reviewable_request (id, hash, body, requestor, reviewer, reference, reject_reason, "
                  "created_at, version, lastmodified, all_tasks, pending_tasks, external_details) "
                  "VALUES (:id, :hash, :body, :requestor, :reviewer, :reference, :reject_reason, :created, :v, :lm, :at, :pt, :ed)";
        }
        else
        {
            sql = "UPDATE reviewable_request SET hash=:hash, body = :body, requestor = :requestor, reviewer = :reviewer, "
                  "reference = :reference, reject_reason = :reject_reason, created_at = :created, version=:v, "
                  "lastmodified=:lm, all_tasks = :at, pending_tasks = :pt, external_details = :ed "
                  "WHERE id = :id";
        }

        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();

        st.exchange(use(reviewableRequestEntry.requestID, "id"));
        st.exchange(use(hash, "hash"));
        st.exchange(use(strBody, "body"));
        std::string requestor = PubKeyUtils::toStrKey(reviewableRequestFrame->getRequestor());
        st.exchange(use(requestor, "requestor"));
        auto reviewer = PubKeyUtils::toStrKey(reviewableRequestEntry.reviewer);
        st.exchange(use(reviewer, "reviewer"));
        st.exchange(use(reviewableRequestEntry.reference, "reference"));
        st.exchange(use(rejectReason, "reject_reason"));
        st.exchange(use(reviewableRequestEntry.createdAt, "created"));
        st.exchange(use(version, "v"));
        st.exchange(use(reviewableRequestFrame->mEntry.lastModifiedLedgerSeq, "lm"));
        st.exchange(use(allTasks, "at"));
        st.exchange(use(pendingTasks, "pt"));
        st.exchange(use(strExternalDetails, "ed"));
        st.define_and_bind();

        auto timer = insert ? db.getInsertTimer("reviewable_request") : db.getUpdateTimer("reviewable_request");
        st.execute(true);

        if (st.get_affected_rows() != 1)
        {
            throw std::runtime_error("could not update SQL");
        }

        if (insert)
        {
            delta.addEntry(*reviewableRequestFrame);
        }
        else
        {
            delta.modEntry(*reviewableRequestFrame);
        }
    }

    void ReviewableRequestHelper::loadRequests(StatementContext &prep,
                                               std::function<void(LedgerEntry const &)> requestsProcessor) {
        LedgerEntry le;
        le.data.type(LedgerEntryType::REVIEWABLE_REQUEST);
        ReviewableRequestEntry& oe = le.data.reviewableRequest();
        std::string hash, body, rejectReason, externalDetails;
        int version;
        uint32_t allTasks, pendingTasks;

        statement& st = prep.statement();
        st.exchange(into(oe.requestID));
        st.exchange(into(hash));
        st.exchange(into(body));
        st.exchange(into(oe.requestor));
        st.exchange(into(oe.reviewer));
        st.exchange(into(oe.reference));
        st.exchange(into(rejectReason));
        st.exchange(into(oe.createdAt));
        st.exchange(into(version));
        st.exchange(into(le.lastModifiedLedgerSeq));
        st.exchange(into(allTasks));
        st.exchange(into(pendingTasks));
        st.exchange(into(externalDetails));
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
            oe.ext.v(static_cast<LedgerVersion>(version));

            if(oe.ext.v() < LedgerVersion::ADD_TASKS_TO_REVIEWABLE_REQUEST && allTasks != 0)
                throw std::runtime_error("version is incorrect but AllTasks in not empty");

            if (oe.ext.v() == LedgerVersion::ADD_TASKS_TO_REVIEWABLE_REQUEST)
            {
                oe.ext.tasksExt().allTasks = allTasks;
                oe.ext.tasksExt().pendingTasks = pendingTasks;

                // unmarshal external details
                std::vector<uint8_t> decodedDetails;
                bn::decode_b64(externalDetails, decodedDetails);
                xdr::xdr_get detailsUnmarshaler(&decodedDetails.front(), &decodedDetails.back() + 1);
                xdr::xdr_argpack_archive(detailsUnmarshaler, oe.ext.tasksExt().externalDetails);
                detailsUnmarshaler.done();
            }

            ReviewableRequestFrame::ensureValid(oe);

            requestsProcessor(le);
            st.fetch();
        }
    }

    bool ReviewableRequestHelper::exists(Database &db, AccountID const &rawRequestor, stellar::string64 reference, uint64_t requestID) {
        auto timer = db.getSelectTimer("reviewable_request_exists_by_reference");
        auto prep =
                db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM reviewable_request WHERE requestor=:requestor AND reference = :reference AND id <> :request_id)");
        auto& st = prep.statement();
        auto requestor = PubKeyUtils::toStrKey(rawRequestor);
        st.exchange(use(requestor, "requestor"));
        st.exchange(use(reference, "reference"));
        st.exchange(use(requestID, "request_id"));
        int exists = 0;
        st.exchange(into(exists));
        st.define_and_bind();
        st.execute(true);

        return exists != 0;
    }

    bool
    ReviewableRequestHelper::isReferenceExist(Database &db, AccountID const &requestor, string64 reference, const uint64_t requestID) {
        if (exists(db, requestor, reference, requestID))
            return true;
        LedgerKey key;
        key.type(LedgerEntryType::REFERENCE_ENTRY);
        key.reference().reference = reference;
        key.reference().sender = requestor;
        return EntryHelperProvider::existsEntry(db, key);
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

        auto pEntry = std::make_shared<LedgerEntry>(retReviewableRequest->mEntry);
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

vector<ReviewableRequestFrame::pointer> ReviewableRequestHelper::
loadRequests(AccountID const& rawRequestor, ReviewableRequestType requestType,
    Database& db)
{
    std::string sql = selectorReviewableRequest;
    sql += +" WHERE requestor = :requstor";
    auto prep = db.getPreparedStatement(sql);
    auto& st = prep.statement();
    auto requestor = PubKeyUtils::toStrKey(rawRequestor);
    st.exchange(use(requestor));

    vector<ReviewableRequestFrame::pointer> result;
    auto timer = db.getSelectTimer("reviewable_request");
    loadRequests(prep, [&result,requestType](LedgerEntry const& entry)
    {
        auto request = make_shared<ReviewableRequestFrame>(entry);
        if (request->getRequestType() != requestType)
            return;
        result.push_back(request);
    });

    return result;
}

string
ReviewableRequestHelper::obtainSqlRequestIDsString(std::vector<uint64_t> requestIDs)
{
    string result;
    for (auto requestID : requestIDs)
    {
        result += to_string(requestID);
        result += ", ";
    }

    return result.substr(0, result.size() - 2);
}

vector<ReviewableRequestFrame::pointer>
ReviewableRequestHelper::loadRequests(std::vector<uint64_t> requestIDs, Database& db)
{
    if (requestIDs.size() == 0)
        return vector<ReviewableRequestFrame::pointer>{};

    string sql = selectorReviewableRequest;
    sql += " WHERE id IN (" + obtainSqlRequestIDsString(requestIDs) + ")";
    auto prep = db.getPreparedStatement(sql);

    vector<ReviewableRequestFrame::pointer> result;
    auto timer = db.getSelectTimer("reviewable_request");
    loadRequests(prep, [&result](LedgerEntry const& entry)
    {
        result.emplace_back(make_shared<ReviewableRequestFrame>(entry));
    });

    return result;
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

    EntryFrame::pointer ReviewableRequestHelper::storeLoad(LedgerKey const &key, Database &db) {
        return loadRequest(key.reviewableRequest().requestID, db);
    }
}