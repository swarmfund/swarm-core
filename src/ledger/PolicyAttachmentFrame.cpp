#include "PolicyAttachmentFrame.h"
#include "AccountFrame.h"

using namespace soci;
using namespace std;

namespace stellar {
    using xdr::operator<;

    PolicyAttachmentFrame::PolicyAttachmentFrame() : EntryFrame(LedgerEntryType::POLICY_ATTACHMENT),
                                                     mPolicyAttachment(mEntry.data.policyAttachment()) {

    }

    PolicyAttachmentFrame::PolicyAttachmentFrame(LedgerEntry const &from) : EntryFrame(from), mPolicyAttachment(
            mEntry.data.policyAttachment()) {

    }

    PolicyAttachmentFrame::PolicyAttachmentFrame(PolicyAttachmentFrame const &from)
            : PolicyAttachmentFrame(from.mEntry) {

    }

    PolicyAttachmentFrame &PolicyAttachmentFrame::operator=(const PolicyAttachmentFrame &other) {
        if (&other != this) {
            mPolicyAttachment = other.mPolicyAttachment;
            mKey = other.mKey;
            mKeyCalculated = other.mKeyCalculated;
        }
        return *this;
    }

    void PolicyAttachmentFrame::ensureValid(const PolicyAttachmentEntry &entry) {
    }

    void PolicyAttachmentFrame::ensureValid() const {
        return ensureValid(mPolicyAttachment);
    }

    PolicyAttachmentFrame::pointer
    PolicyAttachmentFrame::createNew(uint64_t policyAttachmentID, AccountID const &ownerID,
                                     CreatePolicyAttachment const &creationData, LedgerDelta &delta) {
        LedgerEntry entry;
        entry.data.type(LedgerEntryType::POLICY_ATTACHMENT);
        auto &policyAttachment = entry.data.policyAttachment();
        policyAttachment.policyAttachmentID = policyAttachmentID;
        policyAttachment.policyID = creationData.policyID;
        policyAttachment.ownerID = ownerID;
        policyAttachment.actor.type(creationData.actor.type());

        switch (policyAttachment.actor.type())
        {
            case PolicyAttachmentType::FOR_ACCOUNT_TYPE:
                policyAttachment.actor.accountType() = creationData.actor.accountType();
                break;
            case PolicyAttachmentType::FOR_ACCOUNT_ID:
                policyAttachment.actor.accountID() = creationData.actor.accountID();
                break;
            case PolicyAttachmentType::FOR_ANY_ACCOUNT:
                break;
            default:
                throw runtime_error("bad policy attachment actor type");
        }

        return make_shared<PolicyAttachmentFrame>(entry);
    }
}