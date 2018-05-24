#pragma once

#include "ledger/EntryFrame.h"

namespace soci {
    class session;
}

namespace stellar {
    class StatementContext;

    class PolicyAttachmentFrame : public EntryFrame {
        PolicyAttachmentEntry &mPolicyAttachment;

        PolicyAttachmentFrame(PolicyAttachmentFrame const &from);

    public:
        typedef std::shared_ptr<PolicyAttachmentFrame> pointer;

        PolicyAttachmentFrame();

        explicit PolicyAttachmentFrame(LedgerEntry const &from);

        PolicyAttachmentFrame &operator=(PolicyAttachmentFrame const &other);

        EntryFrame::pointer copy() const override {
            return EntryFrame::pointer(new PolicyAttachmentFrame(*this));
        }

        PolicyAttachmentEntry const &getPolicyAttachment() const {
            return mPolicyAttachment;
        }

        uint64_t getID() const {
            return mPolicyAttachment.policyAttachmentID;
        }

        uint64_t getPolicyID() const {
            return mPolicyAttachment.policyID;
        }

        AccountID const &getOwnerID() const {
            return mPolicyAttachment.ownerID;
        }

        static void ensureValid(PolicyAttachmentEntry const &entry);

        void ensureValid() const;

        static pointer
        createNew(uint64_t policyAttachmentID, AccountID const &ownerID,
                  CreatePolicyAttachment const &creationData, LedgerDelta &delta);
    };
}