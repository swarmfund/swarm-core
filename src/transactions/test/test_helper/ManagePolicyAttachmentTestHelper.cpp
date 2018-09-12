#include "ManagePolicyAttachmentTestHelper.h"
#include "transactions/SetAccountRoleOpFrame.h"
#include "test/test_marshaler.h"

namespace stellar {
    namespace txtest {

        ManagePolicyAttachmentTestHelper::ManagePolicyAttachmentTestHelper(TestManager::pointer testManager)
                : TxHelper(testManager) {
        }

        CreatePolicyAttachment::_actor_t
        ManagePolicyAttachmentTestHelper::createActorForAccountType(AccountType accountType) {
            CreatePolicyAttachment::_actor_t actor;
            actor.type(PolicyAttachmentType::FOR_ACCOUNT_TYPE);
            actor.accountType() = accountType;
            return actor;
        }

        CreatePolicyAttachment::_actor_t
        ManagePolicyAttachmentTestHelper::createActorForAccountID(AccountID accountID) {
            CreatePolicyAttachment::_actor_t actor;
            actor.type(PolicyAttachmentType::FOR_ACCOUNT_ID);
            actor.accountID() = accountID;
            return actor;
        }

        CreatePolicyAttachment::_actor_t ManagePolicyAttachmentTestHelper::createActorForAnyAccount() {
            CreatePolicyAttachment::_actor_t actor;
            actor.type(PolicyAttachmentType::FOR_ANY_ACCOUNT);
            return actor;
        }

        ManagePolicyAttachmentOp::_opInput_t
        ManagePolicyAttachmentTestHelper::createCreationOpInput(uint64_t policyID,
                                                                CreatePolicyAttachment::_actor_t actor) {
            ManagePolicyAttachmentOp::_opInput_t opInput;
            opInput.action(ManagePolicyAttachmentAction::CREATE_POLICY_ATTACHMENT);
            opInput.creationData().policyID = policyID;
            opInput.creationData().actor = actor;
            return opInput;
        }

        ManagePolicyAttachmentOp::_opInput_t
        ManagePolicyAttachmentTestHelper::createDeletionOpInput(uint64_t policyAttachmentID) {
            ManagePolicyAttachmentOp::_opInput_t opInput;
            opInput.action(ManagePolicyAttachmentAction::DELETE_POLICY_ATTACHMENT);
            opInput.deletionData().policyAttachmentID = policyAttachmentID;
            return opInput;
        }

        TransactionFramePtr
        ManagePolicyAttachmentTestHelper::createManagePolicyAttachmentTx(Account &source,
                                                                         ManagePolicyAttachmentOp::_opInput_t opInput) {
            Operation baseOp;
            baseOp.body.type(OperationType::MANAGE_POLICY_ATTACHMENT);
            auto &op = baseOp.body.managePolicyAttachmentOp();
            op.opInput = opInput;
            return txFromOperation(source, baseOp, nullptr);
        }

        ManagePolicyAttachmentResult
        ManagePolicyAttachmentTestHelper::applyManagePolicyAttachment(Account &source,
                                                                      ManagePolicyAttachmentOp::_opInput_t opInput,
                                                                      ManagePolicyAttachmentResultCode expectedResultCode) {
            auto &db = mTestManager->getDB();

            TransactionFramePtr txFrame;
            txFrame = createManagePolicyAttachmentTx(source, opInput);
            mTestManager->applyCheck(txFrame);

            auto txResult = txFrame->getResult();
            auto actualResultCode = SetAccountRoleOpFrame::getInnerCode(txResult.result.results()[0]);

            REQUIRE(actualResultCode == expectedResultCode);

            auto txFee = mTestManager->getApp().getLedgerManager().getTxFee();
            REQUIRE(txResult.feeCharged == txFee);

            auto opResult = txResult.result.results()[0].tr().managePolicyAttachmentResult();



            return opResult;
        }
    }
}