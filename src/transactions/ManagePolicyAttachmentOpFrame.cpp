#include "ManagePolicyAttachmentOpFrame.h"
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
        return SourceDetails({AccountType::MASTER}, mSourceAccount->getHighThreshold(),
                             static_cast<int32_t>(SignerType::IDENTITY_POLICY_MANAGER));
    }

    bool ManagePolicyAttachmentOpFrame::deletePolicyAttachment(Application &app, LedgerDelta &delta, Database &db) {
        auto policyAttachmentHelper = PolicyAttachmentHelper::Instance();
        auto policyAttachmentFrame = policyAttachmentHelper->loadPolicyAttachment(
                mManagePolicyAttachment.opInput.deletionData().policyAttachmentID, db, &delta);

        if (!policyAttachmentFrame) {
            innerResult().code(ManagePolicyAttachmentResultCode::POLICY_ATTACHMENT_NOT_FOUND);
            return false;
        }

        EntryHelperProvider::storeDeleteEntry(delta, db, policyAttachmentFrame->getKey());

        innerResult().code(ManagePolicyAttachmentResultCode::SUCCESS);
        innerResult().managePolicyAttachmentSuccess().policyAttachmentID = policyAttachmentFrame->getID();
        return true;
    }

    // check policy presence -> if actor is account_id ? check account presence -> check duplication ->
    bool ManagePolicyAttachmentOpFrame::doApply(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager) {
        Database &db = ledgerManager.getDatabase();

        if (mManagePolicyAttachment.opInput.action() == ManagePolicyAttachmentAction::DELETE_POLICY_ATTACHMENT) {
            return deletePolicyAttachment(app, delta, db);
        }

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