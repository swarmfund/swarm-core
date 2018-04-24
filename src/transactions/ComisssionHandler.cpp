//
// Created by dmytriiev on 31.03.18.
//

#include "ComisssionHandler.h"

 ComissionResult ComisssionHandler::fromBalance(BalanceFrame& balance, uint64& amount, FeeType feeType, const Fee fee, Database &db) {
     auto accountHelper = AccountHelper::Instance();
     auto accountFrame = accountHelper->loadAccount(balance.getAccountID(),db);
     auto result = FeeManager::calcualteFeeForAccount(accountFrame, feeType, balance.getAsset(), FeeFrame::SUBTYPE_ANY, amount, db);

     if(isSystemAccountType(accountFrame->getAccountType()))
     {
         if(fee.percent !=0 || fee.fixed != 0)
             return ComissionResult ::FEE_MISMATCH;
     }

     if(result.fixedFee != fee.fixed || result.calculatedPercentFee != fee.percent)
        return ComissionResult ::FEE_MISMATCH;

     if(result.isOverflow)
         return ComissionResult ::FEE_OVERFLOW;

     uint64_t totalFee = 0;
     if(!safeSum(result.fixedFee,result.calculatedPercentFee, totalFee))
         return ComissionResult ::FEE_OVERFLOW;

     if(balance.getAmount() < totalFee)
         return ComissionResult ::MALFORMED;

     if(!balance.addBalance(-totalFee))
         return ComissionResult ::CANT_ADD_BALANCE;

     return SUCCESS;
}

ComissionResult ComisssionHandler::fromAmount(BalanceFrame& balance, uint64& amount, FeeType feeType, const Fee fee, Database &db) {
    auto accountHelper = AccountHelper::Instance();
    auto accountFrame = accountHelper->loadAccount(balance.getAccountID(),db);
    auto result = FeeManager::calcualteFeeForAccount(accountFrame, feeType, balance.getAsset(), FeeFrame::SUBTYPE_ANY, amount, db);

    if(isSystemAccountType(accountFrame->getAccountType()))
    {
        if(fee.percent !=0 || fee.fixed != 0)
            return ComissionResult ::FEE_MISMATCH;
    }

    if(result.fixedFee != fee.fixed || result.percentFee != fee.percent)
        return ComissionResult ::FEE_MISMATCH;

    if(result.isOverflow)
        return ComissionResult ::FEE_OVERFLOW;

    uint64_t totalFee = 0;
    if(!safeSum(result.fixedFee,result.calculatedPercentFee, totalFee))
        return ComissionResult ::FEE_OVERFLOW;

    if(amount < totalFee)
        return ComissionResult ::MALFORMED;

    amount -= totalFee;
    return ComissionResult::SUCCESS;
}