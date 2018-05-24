#pragma once

#include "ledger/LedgerManager.h"
#include "EntryHelper.h"
#include "PolicyAttachmentFrame.h"

namespace soci {
    class session;
}

namespace stellar {
    class StatementContext;

    class PolicyAttachmentHelper : public EntryHelper {
    public:
        static PolicyAttachmentHelper *Instance() {
            static PolicyAttachmentHelper singleton;
            return &singleton;
        }

        PolicyAttachmentHelper(PolicyAttachmentHelper const &) = delete;

        PolicyAttachmentHelper &operator=(PolicyAttachmentHelper const &) = delete;

        void dropAll(Database &db) override;

        void storeAdd(LedgerDelta &delta, Database &db, LedgerEntry const &entry) override;

        void storeChange(LedgerDelta &delta, Database &db, LedgerEntry const &entry) override;

        void storeDelete(LedgerDelta &delta, Database &db, LedgerKey const &key) override;

        bool exists(Database &db, LedgerKey const &key) override;

        bool exists(Database &db, uint64_t policyID, AccountID const &ownerID,
                    CreatePolicyAttachment::_actor_t const &actor);

        LedgerKey getLedgerKey(LedgerEntry const &from) override;

        EntryFrame::pointer storeLoad(LedgerKey const &key, Database &db) override;

        EntryFrame::pointer fromXDR(LedgerEntry const &from) override;

        uint64_t countObjects(soci::session &sess) override;

        uint64_t countObjects(Database &db, AccountID const& ownerID);

        PolicyAttachmentFrame::pointer
        loadPolicyAttachment(uint64_t policyAttachmentID, Database &db, LedgerDelta *delta = nullptr);

        PolicyAttachmentFrame::pointer
        loadPolicyAttachment(uint64_t policyAttachmentID, AccountID const& ownerID,
                             Database &db, LedgerDelta *delta = nullptr);

        void
        loadPolicyAttachments(AccountType const &accountType,
                              std::vector<PolicyAttachmentFrame::pointer> &retAttachments, Database &db);

        void
        loadPolicyAttachments(AccountID const &accountID,
                              std::vector<PolicyAttachmentFrame::pointer> &retAttachments, Database &db);

    private:
        PolicyAttachmentHelper() { ; }

        ~PolicyAttachmentHelper() { ; }

        static void loadPolicyAttachments(StatementContext &prep,
                                          std::function<void(LedgerEntry const &)> policyAttachmentProcessor);

        void storeUpdateHelper(LedgerDelta &delta, Database &db, bool insert, LedgerEntry const &entry);
    };
}