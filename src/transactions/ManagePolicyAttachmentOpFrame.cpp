#include "ManagePolicyAttachmentOpFrame.h"
#include "ledger/LedgerDelta.h"
#include "ledger/AccountHelper.h"
#include "ledger/IdentityPolicyHelper.h"
#include "ledger/PolicyAttachmentHelper.h"

namespace stellar {
    using namespace std;

    uint32_t const ManagePolicyAttachmentOpFrame::sPolicyAttachmentsLimit = 100;

    ManagePolicyAttachmentOpFrame::ManagePolicyAttachmentOpFrame(Operation const &op, OperationResult &res,
                                                                 TransactionFrame &parentTx)
            : OperationFrame(op, res, parentTx), mManagePolicyAttachment(mOperation.body.managePolicyAttachmentOp()) {

    }

    std::unordered_map<AccountID, CounterpartyDetails>
    ManagePolicyAttachmentOpFrame::getCounterpartyDetails(Database &db, LedgerDelta *delta) const {
        //no counterparties
        return {};
    }

    SourceDetails ManagePolicyAttachmentOpFrame::getSourceAccountDetails(
            std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails, int32_t ledgerVersion) const {
        return SourceDetails({AccountType::ANY}, mSourceAccount->getHighThreshold(),
                             static_cast<int32_t>(SignerType::IDENTITY_POLICY_MANAGER));
    }

    bool ManagePolicyAttachmentOpFrame::deletePolicyAttachment(Application &app, LedgerDelta &delta, Database &db) {
        auto policyAttachmentHelper = PolicyAttachmentHelper::Instance();
        auto policyAttachmentFrame = policyAttachmentHelper->loadPolicyAttachment(
                mManagePolicyAttachment.opInput.deletionData().policyAttachmentID, getSourceID(), db, &delta);

        if (!policyAttachmentFrame) {
            innerResult().code(ManagePolicyAttachmentResultCode::POLICY_ATTACHMENT_NOT_FOUND);
            return false;
        }

        EntryHelperProvider::storeDeleteEntry(delta, db, policyAttachmentFrame->getKey());

        innerResult().code(ManagePolicyAttachmentResultCode::SUCCESS);
        innerResult().managePolicyAttachmentSuccess().policyAttachmentID = policyAttachmentFrame->getID();
        return true;
    }

    bool ManagePolicyAttachmentOpFrame::doApply(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager) {
        Database &db = ledgerManager.getDatabase();

        if (mManagePolicyAttachment.opInput.action() == ManagePolicyAttachmentAction::DELETE_POLICY_ATTACHMENT) {
            return deletePolicyAttachment(app, delta, db);
        }

        // create
        auto policyFrame = IdentityPolicyHelper::Instance()->loadIdentityPolicy(
                mManagePolicyAttachment.opInput.creationData().policyID, getSourceID(), db, &delta
        );
        if (!policyFrame) {
            innerResult().code(ManagePolicyAttachmentResultCode::POLICY_NOT_FOUND);
            return false;
        }

        auto actor = mManagePolicyAttachment.opInput.creationData().actor;

        if (actor.type() == PolicyAttachmentType::FOR_ACCOUNT_ID
            && !AccountHelper::Instance()->exists(actor.accountID(), db)) {
            innerResult().code(ManagePolicyAttachmentResultCode::DESTINATION_ACCOUNT_NOT_FOUND);
            return false;
        }

        auto policyAttachmentHelper = PolicyAttachmentHelper::Instance();

        if (policyAttachmentHelper->exists(db, mManagePolicyAttachment.opInput.creationData().policyID, getSourceID(),
                                           actor)) {
            innerResult().code(ManagePolicyAttachmentResultCode::ATTACHMENT_ALREADY_EXISTS);
            return false;
        }

        if (policyAttachmentHelper->countObjects(db, getSourceID()) ==
            ManagePolicyAttachmentOpFrame::sPolicyAttachmentsLimit) {
            innerResult().code(ManagePolicyAttachmentResultCode::POLICY_ATTACHMENTS_LIMIT_EXCEEDED);
            return false;
        }

        auto newPolicyAttachmentID = delta.getHeaderFrame().generateID(LedgerEntryType::POLICY_ATTACHMENT);

        auto policyAttachmentFrame = PolicyAttachmentFrame::createNew(newPolicyAttachmentID, getSourceID(),
                                                                      mManagePolicyAttachment.opInput.creationData(),
                                                                      delta);

        policyAttachmentHelper->storeAdd(delta, db, policyAttachmentFrame->mEntry);

        innerResult().code(ManagePolicyAttachmentResultCode::SUCCESS);
        innerResult().managePolicyAttachmentSuccess().policyAttachmentID = newPolicyAttachmentID;
        return true;
    }

    bool ManagePolicyAttachmentOpFrame::doCheckValid(Application &app) {
        if (mManagePolicyAttachment.opInput.action() == ManagePolicyAttachmentAction::DELETE_POLICY_ATTACHMENT &&
            mManagePolicyAttachment.opInput.deletionData().policyAttachmentID == 0) {
            innerResult().code(ManagePolicyAttachmentResultCode::POLICY_ATTACHMENT_NOT_FOUND);
            return false;
        }

        if (mManagePolicyAttachment.opInput.action() == ManagePolicyAttachmentAction::CREATE_POLICY_ATTACHMENT &&
            mManagePolicyAttachment.opInput.creationData().policyID == 0) {
            innerResult().code(ManagePolicyAttachmentResultCode::POLICY_NOT_FOUND);
            return false;
        }

        return true;
    }
}