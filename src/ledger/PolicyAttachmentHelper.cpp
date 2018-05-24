#include "PolicyAttachmentHelper.h"
#include "LedgerDelta.h"

using namespace soci;
using namespace std;

namespace stellar {
    using xdr::operator<;

    const int32_t EMPTY_VALUE = -1;

    static const char *policyAttachmentColumnSelector = "SELECT policy_attachment_id, policy_id, owner_id, "
                                                        "account_type, account_id, lastmodified, version "
                                                        "FROM policy_attachment";

    void PolicyAttachmentHelper::dropAll(Database &db) {
        db.getSession() << "DROP TABLE IF EXISTS policy_attachment;";
        db.getSession() << "CREATE TABLE policy_attachment"
                           "("
                           "policy_attachment_id    BIGINT      NOT NULL CHECK (policy_attachment_id >= 0), "
                           "policy_id               BIGINT      NOT NULL CHECK (policy_id >= 0), "
                           "owner_id                VARCHAR(56) NOT NULL, "
                           "account_type            INT, "
                           "account_id              VARCHAR(56), "
                           "lastmodified            INT         NOT NULL, "
                           "version                 INT         NOT NULL DEFAULT 0, "
                           "PRIMARY KEY (policy_attachment_id)"
                           ");";
    }

    void
    PolicyAttachmentHelper::storeUpdateHelper(LedgerDelta &delta, Database &db, bool insert, LedgerEntry const &entry) {
        auto policyAttachmentFrame = make_shared<PolicyAttachmentFrame>(entry);
        auto policyAttachmentEntry = policyAttachmentFrame->getPolicyAttachment();

        policyAttachmentFrame->touch(delta);

        policyAttachmentFrame->ensureValid();

        string sql;

        if (insert) {
            sql = "INSERT INTO policy_attachment (policy_attachment_id, policy_id, owner_id, account_type, account_id, "
                  "lastmodified, version) VALUES (:paid, :pid, :oid, :at, :aid, :lm, :v)";
        } else {
            sql = "UPDATE policy_attachment "
                  "SET policy_id = :pid, owner_id = :oid, account_type = :at, account_id = :aid, lastmodified = :lm, version = :v "
                  "WHERE policy_attachment_id = :paid";
        }

        auto prep = db.getPreparedStatement(sql);
        auto &st = prep.statement();

        uint64_t policyAttachmentID = policyAttachmentFrame->getID();
        uint64_t policyID = policyAttachmentFrame->getPolicyID();
        string ownerID = PubKeyUtils::toStrKey(policyAttachmentFrame->getOwnerID());
        auto policyAttachmentVersion = static_cast<int32_t>(policyAttachmentFrame->getPolicyAttachment().ext.v());

        st.exchange(use(policyAttachmentID, "paid"));
        st.exchange(use(policyID, "pid"));
        st.exchange(use(ownerID, "oid"));

        int32_t accountType = EMPTY_VALUE;
        if (policyAttachmentEntry.actor.type() == PolicyAttachmentType::FOR_ACCOUNT_TYPE) {
            accountType = static_cast<int32_t>(policyAttachmentEntry.actor.accountType());
        }
        st.exchange(use(accountType, "at"));

        string accIDStrKey;
        if (policyAttachmentEntry.actor.type() == PolicyAttachmentType::FOR_ACCOUNT_ID) {
            accIDStrKey = PubKeyUtils::toStrKey(policyAttachmentEntry.actor.accountID());
        }
        st.exchange(use(accIDStrKey, "aid"));

        st.exchange(use(policyAttachmentFrame->mEntry.lastModifiedLedgerSeq, "lm"));
        st.exchange(use(policyAttachmentVersion, "v"));
        st.define_and_bind();

        auto timer = insert ? db.getInsertTimer("policy-attachment") : db.getUpdateTimer("policy-attachment");
        st.execute(true);

        if (st.get_affected_rows() != 1)
            throw runtime_error("could not update SQL");

        if (insert)
            delta.addEntry(*policyAttachmentFrame);
        else
            delta.modEntry(*policyAttachmentFrame);
    }

    void PolicyAttachmentHelper::storeAdd(LedgerDelta &delta, Database &db, LedgerEntry const &entry) {
        storeUpdateHelper(delta, db, true, entry);
    }

    void PolicyAttachmentHelper::storeChange(LedgerDelta &delta, Database &db, LedgerEntry const &entry) {
        storeUpdateHelper(delta, db, false, entry);
    }

    void PolicyAttachmentHelper::storeDelete(LedgerDelta &delta, Database &db, LedgerKey const &key) {
        auto timer = db.getDeleteTimer("policy-attachment");
        auto prep = db.getPreparedStatement("DELETE FROM policy_attachment WHERE policy_attachment_id = :id");
        auto &st = prep.statement();
        auto policyAttachmentID = key.policyAttachment().policyAttachmentID;
        st.exchange(use(policyAttachmentID, "id"));
        st.define_and_bind();
        st.execute(true);
        delta.deleteEntry(key);
    }

    bool PolicyAttachmentHelper::exists(Database &db, LedgerKey const &key) {
        int exists = 0;
        auto timer = db.getSelectTimer("policy-attachment-exists");
        auto prep = db.getPreparedStatement(
                "SELECT EXISTS (SELECT NULL FROM policy_attachment WHERE policy_attachment_id = :id)");
        auto &st = prep.statement();
        auto policyAttachmentID = key.policyAttachment().policyAttachmentID;
        st.exchange(use(policyAttachmentID, "id"));
        st.exchange(into(exists));
        st.define_and_bind();
        st.execute(true);

        return exists != 0;
    }

    bool
    PolicyAttachmentHelper::exists(Database &db, uint64_t policyID, AccountID const &ownerID,
                                   CreatePolicyAttachment::_actor_t const &actor) {
        int exists = 0;
        auto timer = db.getSelectTimer("policy-attachment-exists");
        auto prep = db.getPreparedStatement(
                "SELECT EXISTS (SELECT NULL FROM policy_attachment WHERE policy_id = :pid AND owner_id = :oid "
                "AND account_type = :at AND account_id = :aid)");
        auto &st = prep.statement();

        int32_t accType = EMPTY_VALUE;
        string accIDStrKey;

        switch (actor.type()) {
            case PolicyAttachmentType::FOR_ACCOUNT_ID: {
                accIDStrKey = PubKeyUtils::toStrKey(actor.accountID());
                break;
            }
            case PolicyAttachmentType::FOR_ACCOUNT_TYPE: {
                accType = static_cast<int32_t>(actor.accountType());
                break;
            }
            case PolicyAttachmentType::FOR_ANY_ACCOUNT:
                break;
            default:
                throw runtime_error("Unexpected actor's policy attachment type");
        }

        st.exchange(use(policyID, "pid"));
        st.exchange(use(ownerID, "oid"));
        st.exchange(use(accType, "at"));
        st.exchange(use(accIDStrKey, "aid"));
        st.exchange(into(exists));
        st.define_and_bind();
        st.execute(true);

        return exists != 0;
    }

    LedgerKey stellar::PolicyAttachmentHelper::getLedgerKey(LedgerEntry const &from) {
        LedgerKey ledgerKey;
        ledgerKey.type(from.data.type());
        ledgerKey.policyAttachment().policyAttachmentID = from.data.policyAttachment().policyAttachmentID;
        return ledgerKey;
    }

    EntryFrame::pointer PolicyAttachmentHelper::storeLoad(LedgerKey const &key, Database &db) {
        auto const &policyAttachment = key.policyAttachment();
        return loadPolicyAttachment(policyAttachment.policyAttachmentID, db);
    }

    EntryFrame::pointer PolicyAttachmentHelper::fromXDR(LedgerEntry const &from) {
        return make_shared<PolicyAttachmentFrame>(from);
    }

    uint64_t PolicyAttachmentHelper::countObjects(soci::session &sess) {
        uint64_t count = 0;
        sess << "SELECT COUNT(*) FROM policy_attachment", into(count);
        return count;
    }

    uint64_t PolicyAttachmentHelper::countObjects(Database &db, AccountID const &ownerID) {
        uint64_t count = 0;

        auto timer = db.getSelectTimer("policy-attachment-count-objects");
        auto prep = db.getPreparedStatement("SELECT COUNT(*) FROM policy_attachment WHERE owner_id = :oid");
        auto &st = prep.statement();
        auto ownerIDStrKey = PubKeyUtils::toStrKey(ownerID);
        st.exchange(use(ownerIDStrKey, "oid"));
        st.exchange(into(count));
        st.define_and_bind();
        st.execute(true);

        return count;
    }

    PolicyAttachmentFrame::pointer
    PolicyAttachmentHelper::loadPolicyAttachment(uint64_t policyAttachmentID, Database &db, LedgerDelta *delta) {
        PolicyAttachmentFrame::pointer retPolicyAttachment;

        string sql = policyAttachmentColumnSelector;
        sql += " WHERE policy_attachment_id = :id";
        auto prep = db.getPreparedStatement(sql);
        auto &st = prep.statement();
        st.exchange(use(policyAttachmentID, "id"));

        auto timer = db.getSelectTimer("policy-attachment");
        loadPolicyAttachments(prep, [&retPolicyAttachment](LedgerEntry const &policyAttachment) {
            retPolicyAttachment = make_shared<PolicyAttachmentFrame>(policyAttachment);
        });

        if (delta && retPolicyAttachment) {
            delta->recordEntry(*retPolicyAttachment);
        }

        return retPolicyAttachment;
    }

    PolicyAttachmentFrame::pointer
    PolicyAttachmentHelper::loadPolicyAttachment(uint64_t policyAttachmentID, AccountID const &ownerID,
                                                 Database &db, LedgerDelta *delta) {
        auto policyAttachmentFrame = loadPolicyAttachment(policyAttachmentID, db, delta);
        if (!policyAttachmentFrame)
            return nullptr;
        return policyAttachmentFrame->getOwnerID() == ownerID ? policyAttachmentFrame : nullptr;
    }

    void PolicyAttachmentHelper::loadPolicyAttachments(StatementContext &prep, std::function<void(
            LedgerEntry const &)> policyAttachmentProcessor) {
        LedgerEntry le;
        le.data.type(LedgerEntryType::POLICY_ATTACHMENT);

        int32_t accountType;
        string accIDStrKey;
        int32_t policyAttachmentVersion = 0;

        statement &st = prep.statement();
        st.exchange(into(le.data.policyAttachment().policyAttachmentID));
        st.exchange(into(le.data.policyAttachment().policyID));
        st.exchange(into(accountType));
        st.exchange(into(accIDStrKey));
        st.exchange(into(le.lastModifiedLedgerSeq));
        st.exchange(into(policyAttachmentVersion));

        st.define_and_bind();
        st.execute(true);
        while (st.got_data()) {
            le.data.policyAttachment().ext.v((LedgerVersion) policyAttachmentVersion);

            if (!accIDStrKey.empty() && accountType != EMPTY_VALUE)
                throw runtime_error(
                        "Unexpected state. Policy cannot be attached to account type and account id within one attachment");

            if (accountType != EMPTY_VALUE) {
                le.data.policyAttachment().actor.type(PolicyAttachmentType::FOR_ACCOUNT_TYPE);
                le.data.policyAttachment().actor.accountType() = AccountType(accountType);
            }

            if (!accIDStrKey.empty()) {
                le.data.policyAttachment().actor.type(PolicyAttachmentType::FOR_ACCOUNT_ID);
                le.data.policyAttachment().actor.accountID() = PubKeyUtils::fromStrKey(accIDStrKey);
            }

            PolicyAttachmentFrame::ensureValid(le.data.policyAttachment());

            policyAttachmentProcessor(le);
            st.fetch();
        }
    }

    void PolicyAttachmentHelper::loadPolicyAttachments(AccountType const &accountType,
                                                       std::vector<PolicyAttachmentFrame::pointer> &retAttachments,
                                                       Database &db) {
        auto accType = (int32_t) accountType;

        string sql = policyAttachmentColumnSelector;
        sql += " WHERE account_type = :at";
        auto prep = db.getPreparedStatement(sql);
        auto &st = prep.statement();
        st.exchange(use(accType, "at"));

        auto timer = db.getSelectTimer("policy-attachment");
        loadPolicyAttachments(prep, [&retAttachments](LedgerEntry const &entry) {
            retAttachments.emplace_back(make_shared<PolicyAttachmentFrame>(entry));
        });
    }

    void PolicyAttachmentHelper::loadPolicyAttachments(AccountID const &accountID,
                                                       std::vector<PolicyAttachmentFrame::pointer> &retAttachments,
                                                       Database &db) {
        string accIDStrKey = PubKeyUtils::toStrKey(accountID);

        string sql = policyAttachmentColumnSelector;
        sql += " WHERE account_id = :aid";
        auto prep = db.getPreparedStatement(sql);
        auto &st = prep.statement();
        st.exchange(use(accIDStrKey, "aid"));

        auto timer = db.getSelectTimer("policy-attachment");
        loadPolicyAttachments(prep, [&retAttachments](LedgerEntry const &entry) {
            retAttachments.emplace_back(make_shared<PolicyAttachmentFrame>(entry));
        });
    }
}