

#include "ReviewRequestTestHelper.h"

namespace stellar
{

namespace txtest
{
    class ReviewKYCRequestChecker : public ReviewChecker
    {

    public:
        explicit ReviewKYCRequestChecker(TestManager::pointer);

        void checkApprove(ReviewableRequestFrame::pointer pointer) override;
//

    };

    class ReviewKYCRequestTestHelper : public ReviewRequestHelper
    {
    public:
        explicit ReviewKYCRequestTestHelper(TestManager::pointer testManager);

        ReviewRequestResult
        applyReviewRequestTx(Account &source, uint64_t requestID, Hash requestHash, ReviewableRequestType requestType,
                             ReviewRequestOpAction action, std::string rejectReason,
                             ReviewRequestResultCode expectedResult) override;
        using ReviewRequestHelper::applyReviewRequestTx;


    };
}

}


