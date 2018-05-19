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
}