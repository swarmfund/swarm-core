//
// Created by volodymyr on 02.01.18.
//

#ifndef STELLAR_REVIEWUPDATEKYCREQUESTOPFRAME_H
#define STELLAR_REVIEWUPDATEKYCREQUESTOPFRAME_H

#include "ReviewRequestOpFrame.h"

namespace stellar
{

class ReviewUpdateKYCRequestOpFrame: public ReviewRequestOpFrame
{
protected:
    bool handleApprove(Application& app, LedgerDelta& delta, LedgerManager& ledgerManager, ReviewableRequestFrame::pointer request) override;

    bool handlePermanentReject(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager,
                               ReviewableRequestFrame::pointer request) override;

public:
    ReviewUpdateKYCRequestOpFrame(Operation const& op, OperationResult& opRes, TransactionFrame& parentTx);

protected:
    SourceDetails
    getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails) const override;

};

}




#endif //STELLAR_REVIEWUPDATEKYCREQUESTOPFRAME_H
