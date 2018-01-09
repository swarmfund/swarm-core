//
// Created by volodymyr on 07.01.18.
//

#ifndef STELLAR_REVIEWKYCREQUESTTESTHELPER_H
#define STELLAR_REVIEWKYCREQUESTTESTHELPER_H

#include "ReviewRequestTestHelper.h"

namespace stellar
{

namespace txtest
{
    class ReviewKYCRequestChecker : public ReviewChecker
    {
    private:
        std::shared_ptr<UpdateKYCRequest> updateKYCRequest;
        AccountFrame::pointer accountBeforeTx;
    public:
        explicit ReviewKYCRequestChecker(TestManager::pointer, uint64_t requestID);

        void checkApprove(ReviewableRequestFrame::pointer pointer) override;
    };

    class ReviewKYCRequestTestHelper : public ReviewRequestHelper
    {
    public:
        explicit ReviewKYCRequestTestHelper(TestManager::pointer testManager);

        ReviewRequestResult
        applyReviewRequestTx(Account &source, uint64_t requestID, Hash requestHash, ReviewableRequestType requestType,
                             ReviewRequestOpAction action, std::string rejectReason,
                             ReviewRequestResultCode expectedResult) override;

        ReviewRequestResult applyReviewRequestTx(Account &source, uint64_t requestID, ReviewRequestOpAction action,
                                                 std::string rejectReason,
                                                 ReviewRequestResultCode expectedResult = ReviewRequestResultCode::SUCCESS) override;

    };
}

}



#endif //STELLAR_REVIEWKYCREQUESTTESTHELPER_H