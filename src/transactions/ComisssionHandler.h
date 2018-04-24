//
// Created by dmytriiev on 31.03.18.
//

#ifndef STELLAR_COMISSIONHANDLER_H
#define STELLAR_COMISSIONHANDLER_H

#include "transactions/OperationFrame.h"
#include "ledger/InvoiceFrame.h"
#include "ledger/FeeHelper.h"
#include "ledger/AccountHelper.h"
#include "FeesManager.h"

enum ComissionResult{
    SUCCESS =0,
    FEE_MISMATCH=1,
    MALFORMED=2,
    FEE_OVERFLOW=3,
    CANT_ADD_BALANCE,
    SMTH_WRONG=-1
};

class ComisssionHandler {
    ComissionResult
    fromAmount(BalanceFrame& balance, uint64& amount, FeeType feeType, const Fee fee, Database &db);
    ComissionResult
    fromBalance(BalanceFrame& balance, uint64& amount, FeeType feeType, const Fee fee, Database &db);
};


#endif //STELLAR_COMISSIONHANDLER_H