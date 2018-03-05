//
// Created by volodymyr on 04.02.18.
//

#include <ledger/SaleHelper.h>
#include "ManageSaleOpFrame.h"
#include "xdrpp/printer.h"
#include "ManageSaleHelper.h"

namespace stellar
{

    ManageSaleOpFrame::ManageSaleOpFrame(Operation const &op, OperationResult &opRes, TransactionFrame &parentTx)
            : OperationFrame(op, opRes, parentTx), mManageSaleOp(mOperation.body.manageSaleOp())
    {
    }

    std::unordered_map<AccountID, CounterpartyDetails>
    ManageSaleOpFrame::getCounterpartyDetails(Database &db, LedgerDelta *delta) const
    {
        // no counterparties
        return {};
    }

    SourceDetails ManageSaleOpFrame::getSourceAccountDetails(std::unordered_map<AccountID, CounterpartyDetails> counterpartiesDetails,
                                                             int32_t ledgerVersion) const
    {
        if (ledgerVersion < static_cast<int32_t>(LedgerVersion::ALLOW_TO_MANAGE_SALE))
            return SourceDetails({}, 0, 0);

        auto allowedSigners = static_cast<int32_t>(SignerType::ASSET_MANAGER);
        return SourceDetails({AccountType::SYNDICATE}, mSourceAccount->getHighThreshold(), allowedSigners);
    }

    bool ManageSaleOpFrame::doCheckValid(Application &app)
    {
        if (mManageSaleOp.saleID == 0) {
            innerResult().code(ManageSaleResultCode::NOT_FOUND);
            return false;
        }

        return true;
    }

    bool ManageSaleOpFrame::doApply(Application &app, LedgerDelta &delta, LedgerManager &ledgerManager)
    {
        auto saleFrame = SaleHelper::Instance()->loadSale(mManageSaleOp.saleID, app.getDatabase(), &delta);
        if (!saleFrame || !(saleFrame->getOwnerID() == getSourceID()))
        {
            innerResult().code(ManageSaleResultCode::NOT_FOUND);
            return false;
        }

        switch (mManageSaleOp.action)
        {
            case ManageSaleAction::BLOCK:
                if ( saleFrame->getSaleState() == SaleState::BLOCKED){
                    innerResult().code(ManageSaleResultCode::ALREADY_BLOCKED);
                    return false;
                }
                saleFrame->setSaleState(SaleState::BLOCKED);
                EntryHelperProvider::storeChangeEntry(delta, app.getDatabase(), saleFrame->mEntry);
                break;
            case ManageSaleAction::UNBLOCK:
                if ( saleFrame->getSaleState() == SaleState::ACTIVE){
                    innerResult().code(ManageSaleResultCode::ALREADY_UNBLOCKED);
                    return false;
                }
                saleFrame->setSaleState(SaleState::ACTIVE);
                EntryHelperProvider::storeChangeEntry(delta, app.getDatabase(), saleFrame->mEntry);
                break;
            case ManageSaleAction::DELETE:
                ManageSaleHelper::cancelSale(saleFrame, delta, app.getDatabase());
                break;
            default:
                CLOG(ERROR, Logging::OPERATION_LOGGER) << "Unexpected action from manage sale op: "
                                                       << xdr::xdr_to_string(mManageSaleOp.action);
                throw std::runtime_error("Unexpected action from manage sale op");
        }

        innerResult().code(ManageSaleResultCode::SUCCESS);
        return true;
    }
    std::string ManageSaleOpFrame::getInnerResultCodeAsStr()
    {
        const auto code = getInnerCode(mResult);
        return xdr::xdr_traits<ManageSaleResultCode>::enum_name(code);
    }

}
