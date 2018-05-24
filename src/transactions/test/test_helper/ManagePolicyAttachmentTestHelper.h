#pragma once

#include "TxHelper.h"

namespace stellar {
    namespace txtest {
        class ManagePolicyAttachmentTestHelper : TxHelper {
        public:
            explicit ManagePolicyAttachmentTestHelper(TestManager::pointer testManager);

            CreatePolicyAttachment::_actor_t createActorForAccountType(AccountType accountType);

            CreatePolicyAttachment::_actor_t createActorForAccountID(AccountID accountID);

            CreatePolicyAttachment::_actor_t createActorForAnyAccount();

            ManagePolicyAttachmentOp::_opInput_t createCreationOpInput(uint64_t policyID,
                                                                       CreatePolicyAttachment::_actor_t actor);

            ManagePolicyAttachmentOp::_opInput_t createDeletionOpInput(uint64_t policyAttachmentID);


            TransactionFramePtr createManagePolicyAttachmentTx(Account &source,
                                                               ManagePolicyAttachmentOp::_opInput_t opInput);

            ManagePolicyAttachmentResult
            applyManagePolicyAttachment(Account &source, ManagePolicyAttachmentOp::_opInput_t opInput,
                                        ManagePolicyAttachmentResultCode expectedResultCode =
                                        ManagePolicyAttachmentResultCode::SUCCESS);
        };
    }
}