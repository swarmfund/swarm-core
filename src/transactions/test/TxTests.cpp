// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "main/Application.h"
#include "invariant/Invariants.h"
#include "overlay/LoopbackPeer.h"
#include "util/make_unique.h"
#include "main/test.h"
#include "test/test_marshaler.h"
#include "util/Logging.h"
#include "crypto/ByteSlice.h"
#include "TxTests.h"
#include "util/types.h"
#include "transactions/TransactionFrame.h"
#include "ledger/LedgerDeltaImpl.h"
#include "ledger/FeeFrame.h"
#include "ledger/StatisticsFrame.h"
#include "transactions/payment/PaymentOpFrame.h"
#include "transactions/CreateAccountOpFrame.h"
#include "transactions/ManageBalanceOpFrame.h"
#include "transactions/SetOptionsOpFrame.h"
#include "transactions/DirectDebitOpFrame.h"
#include "transactions/ManageLimitsOpFrame.h"
#include "transactions/ManageInvoiceRequestOpFrame.h"
#include "ledger/AccountHelper.h"
#include "ledger/AssetHelper.h"
#include "ledger/BalanceHelper.h"
#include "ledger/FeeHelper.h"
#include "ledger/StatisticsHelper.h"
#include "crypto/SHA.h"
#include "test_helper/TestManager.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;
namespace stellar
{
using xdr::operator==;

namespace txtest
{
	auto accountHelper = AccountHelper::Instance();
	auto assetHelper = AssetHelper::Instance();
	auto balanceHelper = BalanceHelper::Instance();
	auto feeHelper = FeeHelper::Instance();
	auto statisticsHelper = StatisticsHelper::Instance();


FeeEntry createFeeEntry(FeeType type, int64_t fixed, int64_t percent,
    AssetCode asset, AccountID* accountID, AccountType* accountType, int64_t subtype,
    int64_t lowerBound, int64_t upperBound)
{
	FeeEntry fee;
	fee.feeType = type;
	fee.fixedFee = fixed;
	fee.percentFee = percent;
	fee.asset = asset;
    fee.subtype = subtype;
    if (accountID) {
        fee.accountID.activate() = *accountID;
    }
    if (accountType) {
        fee.accountType.activate() = *accountType;
    }
    fee.lowerBound = lowerBound;
    fee.upperBound = upperBound;

    fee.hash = FeeFrame::calcHash(type, asset, accountID, accountType, subtype);
    
	return fee;
}

PaymentFeeData getNoPaymentFee() {
    return getGeneralPaymentFee(0, 0);
}

PaymentFeeData getGeneralPaymentFee(uint64 fixedFee, uint64 paymentFee) {
    PaymentFeeData paymentFeeData;
    FeeData generalFeeData;
    generalFeeData.fixedFee = fixedFee;
    generalFeeData.paymentFee = paymentFee;
    paymentFeeData.sourceFee = generalFeeData;
    paymentFeeData.destinationFee = generalFeeData;
    paymentFeeData.sourcePaysForDest = true;
    
    return paymentFeeData;
}

[[deprecated("Use TxHelper")]]
bool applyCheck(TransactionFramePtr tx, LedgerDelta& delta, Application& app)
{
    auto txSet = std::make_shared<TxSetFrame>(
            app.getLedgerManager().getLastClosedLedgerHeader().hash);
    txSet->add(tx);

	tx->clearCached();
    bool check = tx->checkValid(app);
    TransactionResult checkResult = tx->getResult();
    REQUIRE((!check || checkResult.result.code() == TransactionResultCode::txSUCCESS));
    
    bool res;
    auto code = checkResult.result.code();

    if (code != TransactionResultCode::txNO_ACCOUNT && code != TransactionResultCode::txDUPLICATION)
    {
        tx->processSeqNum();
    }

    bool doApply = (code != TransactionResultCode::txDUPLICATION);

    if (doApply)
    {
        res = tx->apply(delta, app);


        REQUIRE((!res || tx->getResultCode() == TransactionResultCode::txSUCCESS));

        if (!check)
        {
            REQUIRE(checkResult == tx->getResult());
        }

		if (res)
		{
			delta.commit();
		}
    }
    else
    {
        res = check;
    }

    // verify modified accounts invariants
    auto const& changes = delta.getChanges();
    for (auto const& c : changes)
    {
        switch (c.type())
        {
        case LedgerEntryChangeType::CREATED:
            checkEntry(c.created(), app);
            break;
        case LedgerEntryChangeType::UPDATED:
            checkEntry(c.updated(), app);
            break;
        default:
            break;
        }
    }

    // validates db state
    app.getLedgerManager().checkDbState();
    app.getInvariants().check(txSet, delta);
    return res;
}

void
checkEntry(LedgerEntry const& le, Application& app)
{
    auto& d = le.data;
    switch (d.type())
    {
    case LedgerEntryType::ACCOUNT:
        checkAccount(d.account().accountID, app);
        break;
    default:
        break;
    }
}


void
checkAccount(AccountID const& id, Application& app)
{
    AccountFrame::pointer res =
        accountHelper->loadAccount(id, app.getDatabase());
    REQUIRE(!!res);
}

time_t
getTestDate(int day, int month, int year)
{
    std::tm tm = {0};
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    tm.tm_mday = day;
    tm.tm_mon = month - 1; // 0 based
    tm.tm_year = year - 1900;

    VirtualClock::time_point tp = VirtualClock::tmToPoint(tm);
    time_t t = VirtualClock::to_time_t(tp);

    return t;
}

TxSetResultMeta closeLedgerOn(Application& app, uint32 ledgerSeq, time_t closeTime)
{

	TxSetFramePtr txSet = std::make_shared<TxSetFrame>(
		app.getLedgerManager().getLastClosedLedgerHeader().hash);
	return closeLedgerOn(app, ledgerSeq, closeTime, txSet);
}

TxSetResultMeta
closeLedgerOn(Application& app, uint32 ledgerSeq, int day, int month, int year,
              TransactionFramePtr tx)
{
    TxSetFramePtr txSet = std::make_shared<TxSetFrame>(
        app.getLedgerManager().getLastClosedLedgerHeader().hash);

    if (tx)
    {
        txSet->add(tx);
        txSet->sortForHash();
    }

    return closeLedgerOn(app, ledgerSeq, day, month, year, txSet);
}

TxSetResultMeta closeLedgerOn(Application& app, uint32 ledgerSeq, time_t closeTime, TxSetFramePtr txSet)
{
	StellarValue sv(txSet->getContentsHash(), closeTime,
		emptyUpgradeSteps, StellarValue::_ext_t(LedgerVersion::EMPTY_VERSION));
	LedgerCloseData ledgerData(ledgerSeq, txSet, sv);
	app.getLedgerManager().closeLedger(ledgerData);

	auto z1 = TransactionFrame::getTransactionHistoryResults(app.getDatabase(),
		ledgerSeq);
	auto z2 =
		TransactionFrame::getTransactionFeeMeta(app.getDatabase(), ledgerSeq);

	REQUIRE(app.getLedgerManager().getLedgerNum() == (ledgerSeq + 1));

	TxSetResultMeta res;
	std::transform(z1.results.begin(), z1.results.end(), z2.begin(),
		std::back_inserter(res), [](TransactionResultPair const& r1,
			LedgerEntryChanges const& r2)
	{
		return std::make_pair(r1, r2);
	});

	return res;
}

TxSetResultMeta
closeLedgerOn(Application& app, uint32 ledgerSeq, int day, int month, int year,
              TxSetFramePtr txSet)
{
	return closeLedgerOn(app, ledgerSeq, getTestDate(day, month, year), txSet);
    
}

[[deprecated("Use TestManager")]]
void upgradeToCurrentLedgerVersion(Application& app)
{
	TestManager::upgradeToCurrentLedgerVersion(app);
}

SecretKey
getRoot()
{
	return getMasterKP();
}

SecretKey
getIssuanceKey()
{
	return getIssuanceKP();
}

SecretKey
getAccount(const char* n)
{
	return getAccountSecret(n);
}


AccountFrame::pointer
loadAccount(SecretKey const& k, Application& app, bool mustExist)
{
	return loadAccount(k.getPublicKey(), app, mustExist);
}

AccountFrame::pointer
loadAccount(PublicKey const& k, Application& app, bool mustExist)
{
	AccountFrame::pointer res = accountHelper->loadAccount(k, app.getDatabase());
	if (mustExist)
	{
		REQUIRE(res);
	}
	return res;
}

BalanceFrame::pointer
loadBalance(BalanceID bid, Application& app, bool mustExist)
{
	BalanceFrame::pointer res =
		balanceHelper->loadBalance(bid, app.getDatabase());
	if (mustExist)
	{
		REQUIRE(res);
	}
	return res;
}


void
requireNoAccount(SecretKey const& k, Application& app)
{
    AccountFrame::pointer res = loadAccount(k, app, false);
    REQUIRE(!res);
}

int64_t
getAccountBalance(SecretKey const& k, Application& app)
{
    AccountFrame::pointer account;
    account = loadAccount(k, app);
    std::vector<BalanceFrame::pointer> retBalances;
    balanceHelper->loadBalances(account->getID(), retBalances, app.getDatabase());
    auto balanceFrame = retBalances[0];
    return balanceFrame->getBalance().amount;
}

int64_t
getAccountBalance(PublicKey const& k, Application& app)
{
    AccountFrame::pointer account;
    account = loadAccount(k, app);
    std::vector<BalanceFrame::pointer> retBalances;
    balanceHelper->loadBalances(account->getID(), retBalances, app.getDatabase());
    auto balanceFrame = retBalances[0];
    return balanceFrame->getBalance().amount;
}

int64_t
getBalance(BalanceID const& k, Application& app)
{
    
    auto balance = balanceHelper->loadBalance(k, app.getDatabase());
    assert(balance);
    return balance->getAmount();
}

void
checkTransaction(TransactionFrame& txFrame)
{
    REQUIRE(txFrame.getResult().feeCharged == 0); // default fee
    REQUIRE((txFrame.getResultCode() == TransactionResultCode::txSUCCESS ||
             txFrame.getResultCode() == TransactionResultCode::txFAILED));
}

void
checkTransactionForOpResult(TransactionFramePtr txFrame, Application& app, OperationResultCode opCode)
{
    LedgerDeltaImpl delta(app.getLedgerManager().getCurrentLedgerHeader(),
                          app.getDatabase());
    applyCheck(txFrame, delta, app);
    REQUIRE(getFirstResult(*txFrame).code() == opCode);
}

[[deprecated("Use txHelper")]]
TransactionFramePtr transactionFromOperation(Hash const& networkID, SecretKey& from,
                         Salt salt, Operation const& op, SecretKey* signer = nullptr, TimeBounds* timeBounds = nullptr)
{
    if (!signer)
        signer = &from;
    TransactionEnvelope e;

    e.tx.sourceAccount = from.getPublicKey();
    e.tx.salt = salt;
    e.tx.operations.push_back(op);

    e.tx.timeBounds.minTime = 0;
    e.tx.timeBounds.maxTime = INT64_MAX / 2;
	if (timeBounds)
	{
		e.tx.timeBounds = *timeBounds;
	}

    TransactionFramePtr res =
        TransactionFrame::makeTransactionFromWire(networkID, e);

    res->addSignature(*signer);

    return res;
}


TransactionFramePtr
createCreateAccountTx(Hash const& networkID, SecretKey& from, SecretKey& to,
                      Salt seq, AccountType accountType, AccountID* referrer,
					  TimeBounds* timeBounds, int32 policies)
{
    Operation op;
    op.body.type(OperationType::CREATE_ACCOUNT);
    op.body.createAccountOp().destination = to.getPublicKey();
	op.body.createAccountOp().accountType = accountType;

	if (policies != -1)
	{
		op.body.createAccountOp().policies = policies;
	}

    if (referrer)
        op.body.createAccountOp().referrer.activate() = *referrer;
    return transactionFromOperation(networkID, from, seq, op, nullptr, timeBounds);
}


void
applyCreateAccountTx(Application& app, SecretKey& from, SecretKey& to,
                     Salt seq, AccountType accountType, SecretKey* signer,
                     AccountID* referrer, CreateAccountResultCode result, int32 policies)
{
    TransactionFramePtr txFrame;

    AccountFrame::pointer fromAccount, toAccount;
    toAccount = loadAccount(to, app, false);

    fromAccount = loadAccount(from, app);

    txFrame = createCreateAccountTx(app.getNetworkID(), from, to, seq,
        accountType, referrer, nullptr, policies);
	if (signer)
	{
		txFrame->getEnvelope().signatures.clear();
		txFrame->addSignature(*signer);
	}

    LedgerDeltaImpl delta(app.getLedgerManager().getCurrentLedgerHeader(),
                          app.getDatabase());
    applyCheck(txFrame, delta, app);

    checkTransaction(*txFrame);
    auto txResult = txFrame->getResult();
    auto innerCode =
        CreateAccountOpFrame::getInnerCode(txResult.result.results()[0]);
    REQUIRE(innerCode == result);

    REQUIRE(txResult.feeCharged == app.getLedgerManager().getTxFee());

    AccountFrame::pointer toAccountAfter;
    toAccountAfter = loadAccount(to, app, false);

    if (innerCode != CreateAccountResultCode::SUCCESS)
    {
        // check that the target account didn't change
        REQUIRE(!!toAccount == !!toAccountAfter);
        if (toAccount && toAccountAfter)
        {
            REQUIRE(toAccount->getAccount() == toAccountAfter->getAccount());
        }
    }
    else
    {
        REQUIRE(toAccountAfter);
		REQUIRE(!toAccountAfter->isBlocked());
		REQUIRE(toAccountAfter->getAccountType() == accountType);
        
        auto statisticsFrame = statisticsHelper->loadStatistics(to.getPublicKey(), app.getDatabase());
        REQUIRE(statisticsFrame);
        auto statistics = statisticsFrame->getStatistics();
        REQUIRE(statistics.dailyOutcome == 0);
        REQUIRE(statistics.weeklyOutcome == 0);
        REQUIRE(statistics.monthlyOutcome == 0);
        REQUIRE(statistics.annualOutcome == 0);
        
        if (!toAccount) {
            std::vector<BalanceFrame::pointer> balances;
            balanceHelper->loadBalances(toAccountAfter->getAccount().accountID, balances, app.getDatabase());
			for (BalanceFrame::pointer balance : balances)
			{
				REQUIRE(balance->getBalance().amount == 0);
				REQUIRE(balance->getAccountID() == toAccountAfter->getAccount().accountID);
			}
        }
    }
}

TransactionFramePtr
createManageBalanceTx(Hash const& networkID, SecretKey& from, SecretKey& account,
                      Salt seq, AssetCode asset, ManageBalanceAction action)
{
    Operation op;
    op.body.type(OperationType::MANAGE_BALANCE);
    op.body.manageBalanceOp().destination = account.getPublicKey();
    op.body.manageBalanceOp().action = action;
    op.body.manageBalanceOp().asset = asset;

    return transactionFromOperation(networkID, from, seq, op);
}

ManageBalanceResult
applyManageBalanceTx(Application& app, SecretKey& from, SecretKey& account,
                     Salt seq, AssetCode asset, ManageBalanceAction action,
                     ManageBalanceResultCode result)
{
    TransactionFramePtr txFrame;

    std::vector<BalanceFrame::pointer> balances;
    balanceHelper->loadBalances(account.getPublicKey(), balances, app.getDatabase());
    
    
    txFrame = createManageBalanceTx(app.getNetworkID(), from, account, seq, asset, action);

    LedgerDeltaImpl delta(app.getLedgerManager().getCurrentLedgerHeader(),
                          app.getDatabase());
    applyCheck(txFrame, delta, app);

    checkTransaction(*txFrame);
    auto txResult = txFrame->getResult();
    auto innerCode =
        ManageBalanceOpFrame::getInnerCode(txResult.result.results()[0]);
    REQUIRE(innerCode == result);
    REQUIRE(txResult.feeCharged == app.getLedgerManager().getTxFee());

    std::vector<BalanceFrame::pointer> balancesAfter;
    balanceHelper->loadBalances(account.getPublicKey(), balancesAfter, app.getDatabase());

    auto opResult = txResult.result.results()[0].tr().manageBalanceResult();

    if (innerCode != ManageBalanceResultCode::SUCCESS)
    {
        REQUIRE(balances.size() == balancesAfter.size());
    }
    else
    {
        if (action == ManageBalanceAction::CREATE)
        {
            auto assetFrame = assetHelper->loadAsset(asset, app.getDatabase());
            REQUIRE(balances.size() == balancesAfter.size() - 1);
            auto balance = balanceHelper->loadBalance(opResult.success().balanceID, app.getDatabase());
            REQUIRE(balance);
            REQUIRE(balance->getBalance().accountID == account.getPublicKey());
            REQUIRE(balance->getBalance().amount == 0);
            REQUIRE(balance->getAsset() == asset);
        }
        else
        {
            REQUIRE(balances.size() == balancesAfter.size() + 1);
            REQUIRE(!balanceHelper->loadBalance(opResult.success().balanceID, app.getDatabase()));
        }
    }
    return opResult;
}

TransactionFramePtr
createManageAssetTx(Hash const& networkID, SecretKey& source, Salt seq, AssetCode code,
    int32 policies, ManageAssetAction action)
{
	throw std::runtime_error("use manageAssetHelper");
}

void applyManageAssetTx(Application & app, SecretKey & source, Salt seq, AssetCode asset, int32 policies, ManageAssetAction action, ManageAssetResultCode result)
{
	throw std::runtime_error("use manageAssetHelper");
}


// For base balance
TransactionFramePtr
createPaymentTx(Hash const& networkID, SecretKey& from, SecretKey& to,
                Salt seq, int64_t amount, PaymentFeeData paymentFee, bool isSourceFee, std::string subject, std::string reference, TimeBounds* timeBounds,
                    InvoiceReference* invoiceReference)
{
    return createPaymentTx(networkID, from, from.getPublicKey(), to.getPublicKey(), seq, amount,paymentFee,
        isSourceFee, subject, reference, timeBounds, invoiceReference);
}

TransactionFramePtr
createPaymentTx(Hash const& networkID, SecretKey& from, BalanceID fromBalanceID, BalanceID toBalanceID,
                Salt seq, int64_t amount, PaymentFeeData paymentFee, bool isSourceFee, std::string subject, std::string reference, TimeBounds* timeBounds,
                    InvoiceReference* invoiceReference)
{
    Operation op;
    op.body.type(OperationType::PAYMENT);
    op.body.paymentOp().amount = amount;
	op.body.paymentOp().feeData = paymentFee;
    op.body.paymentOp().subject = subject;
    op.body.paymentOp().sourceBalanceID = fromBalanceID;
    op.body.paymentOp().destinationBalanceID = toBalanceID;
    op.body.paymentOp().reference = reference;
    if (invoiceReference)
        op.body.paymentOp().invoiceReference.activate() = *invoiceReference;

    return transactionFromOperation(networkID, from, seq, op, nullptr, timeBounds);
}


// For base balance
PaymentResult
applyPaymentTx(Application& app, SecretKey& from, SecretKey& to,
               Salt seq, int64_t amount, PaymentFeeData paymentFee, bool isSourceFee,
               std::string subject, std::string reference, PaymentResultCode result,
               InvoiceReference* invoiceReference)
{
    return applyPaymentTx(app, from, from.getPublicKey(), to.getPublicKey(),
               seq, amount, paymentFee, isSourceFee, subject, reference, result, invoiceReference);
}

PaymentResult
applyPaymentTx(Application& app, SecretKey& from, BalanceID fromBalanceID,
        BalanceID toBalanceID, Salt seq, int64_t amount, PaymentFeeData paymentFee,
        bool isSourceFee, std::string subject, std::string reference, 
        PaymentResultCode result, InvoiceReference* invoiceReference)
{
    TransactionFramePtr txFrame;

    txFrame = createPaymentTx(app.getNetworkID(), from, fromBalanceID, toBalanceID,
        seq, amount, paymentFee, isSourceFee, subject, reference, nullptr, invoiceReference);

    LedgerDeltaImpl delta(app.getLedgerManager().getCurrentLedgerHeader(),
                          app.getDatabase());
    applyCheck(txFrame, delta, app);

    checkTransaction(*txFrame);
    auto txResult = txFrame->getResult();
    auto opResult = txResult.result.results()[0];
    REQUIRE(opResult.code() == OperationResultCode::opINNER);
    auto innerCode = PaymentOpFrame::getInnerCode(txResult.result.results()[0]);
    REQUIRE(innerCode == result);
    REQUIRE(txResult.feeCharged == app.getLedgerManager().getTxFee());

    return txResult.result.results()[0].tr().paymentResult();
}

TransactionFramePtr
createSetOptions(Hash const& networkID, SecretKey& source, Salt seq, ThresholdSetter* thrs, Signer* signer, TrustData* trustData)
{
    Operation op;
    op.body.type(OperationType::SET_OPTIONS);

    SetOptionsOp& setOp = op.body.setOptionsOp();

    if (thrs)
    {
        if (thrs->masterWeight)
        {
            setOp.masterWeight.activate() = *thrs->masterWeight;
        }
        if (thrs->lowThreshold)
        {
            setOp.lowThreshold.activate() = *thrs->lowThreshold;
        }
        if (thrs->medThreshold)
        {
            setOp.medThreshold.activate() = *thrs->medThreshold;
        }
        if (thrs->highThreshold)
        {
            setOp.highThreshold.activate() = *thrs->highThreshold;
        }
    }

    if (signer)
    {
        setOp.signer.activate() = *signer;
    }

	if (trustData)
	{
		setOp.trustData.activate() = *trustData;
	}

    return transactionFromOperation(networkID, source, seq, op);
}

void
applySetOptions(Application& app, SecretKey& source, Salt seq,
                ThresholdSetter* thrs, Signer* signer, TrustData* trustData,
                SetOptionsResultCode result, SecretKey* txSiger)
{
    TransactionFramePtr txFrame;

    txFrame = createSetOptions(app.getNetworkID(), source, seq, thrs, signer, trustData);
	if (txSiger)
	{
		txFrame->getEnvelope().signatures.clear();
		txFrame->addSignature(*txSiger);
	}

    LedgerDeltaImpl delta(app.getLedgerManager().getCurrentLedgerHeader(),
                      app.getDatabase());
    applyCheck(txFrame, delta, app);

    checkTransaction(*txFrame);
    REQUIRE(SetOptionsOpFrame::getInnerCode(
                txFrame->getResult().result.results()[0]) == result);
}

TransactionFramePtr
createDirectDebitTx(Hash const& networkID, SecretKey& source, Salt seq, AccountID from,
	PaymentOp paymentOp)
{
	Operation op;
	op.body.type(OperationType::DIRECT_DEBIT);
	op.body.directDebitOp().paymentOp = paymentOp;
	op.body.directDebitOp().from = from;

	return transactionFromOperation(networkID, source, seq, op);
}

DirectDebitResult
applyDirectDebitTx(Application& app, SecretKey& source, Salt seq,
	AccountID from, PaymentOp paymentOp, DirectDebitResultCode result)
{
	TransactionFramePtr txFrame;

	txFrame = createDirectDebitTx(app.getNetworkID(), source, seq, from, paymentOp);

	LedgerDeltaImpl delta(app.getLedgerManager().getCurrentLedgerHeader(),
		app.getDatabase());
	applyCheck(txFrame, delta, app);

	checkTransaction(*txFrame);
	auto txResult = txFrame->getResult();
	auto innerCode =
		DirectDebitOpFrame::getInnerCode(txResult.result.results()[0]);
	REQUIRE(innerCode == result);
	REQUIRE(txResult.feeCharged == app.getLedgerManager().getTxFee());

	auto opResult = txResult.result.results()[0].tr().directDebitResult();

	return opResult;
}

TransactionFramePtr createManageAccount(Hash const& networkID, SecretKey& source, SecretKey& account, Salt seq, uint32 blockReasonsToAdd, uint32 blockReasonsToRemove, AccountType accountType)
{
	Operation op;
	op.body.type(OperationType::MANAGE_ACCOUNT);
	auto& opBody = op.body.manageAccountOp();
	opBody.account = account.getPublicKey();
	opBody.blockReasonsToAdd = blockReasonsToAdd;
    opBody.blockReasonsToRemove = blockReasonsToRemove;
    opBody.accountType = accountType;
	return transactionFromOperation(networkID, source, seq, op);
}

void
applyManageAccountTx(Application& app, SecretKey& source, SecretKey& account, Salt seq, uint32 blockReasonsToAdd, uint32 blockReasonsToRemove, AccountType accountType,  ManageAccountResultCode targetResult)
{
	TransactionFramePtr txFrame = createManageAccount(app.getNetworkID(), source, account,
        seq, blockReasonsToAdd, blockReasonsToRemove, accountType);

	LedgerDeltaImpl delta(app.getLedgerManager().getCurrentLedgerHeader(),
		app.getDatabase());
    
    auto accountFrameBefore = loadAccount(account, app, false);

	applyCheck(txFrame, delta, app);

	auto result = txFrame->getResult().result.results()[0].tr().manageAccountResult();
	REQUIRE(result.code() == targetResult);
	if (result.code() == ManageAccountResultCode::SUCCESS && accountFrameBefore)
	{
		auto accountFrameAfter = loadAccount(account, app);
        uint32 expectedReasons = (accountFrameBefore->getBlockReasons() | blockReasonsToAdd) & ~blockReasonsToRemove; 
        REQUIRE(accountFrameAfter->getBlockReasons() == expectedReasons);
        REQUIRE(accountFrameAfter->isBlocked() == (expectedReasons != 0));
        REQUIRE(result.success().blockReasons == expectedReasons);
	}
}

TransactionFramePtr createSetFees(Hash const& networkID, SecretKey& source, Salt seq, FeeEntry* fee, bool isDelete)
{
	Operation op;
	op.body.type(OperationType::SET_FEES);
	auto& opBody = op.body.setFeesOp();
	if (fee)
		opBody.fee.activate() = *fee;
	opBody.isDelete = isDelete;

	return transactionFromOperation(networkID, source, seq, op);
}

void applySetFees(Application& app, SecretKey& source, Salt seq, FeeEntry* fee, bool isDelete, SecretKey* signer, SetFeesResultCode targetResult)
{
	auto txFrame = createSetFees(app.getNetworkID(), source, seq, fee, isDelete);
	if (signer)
	{
		txFrame->getEnvelope().signatures.clear();
		txFrame->addSignature(*signer);
	}

	LedgerDeltaImpl delta(app.getLedgerManager().getCurrentLedgerHeader(),
		app.getDatabase());

	applyCheck(txFrame, delta, app);

	auto result = txFrame->getResult().result.results()[0].tr().setFeesResult();
	REQUIRE(result.code() == targetResult);
	if (result.code() == SetFeesResultCode::SUCCESS)
	{
		if (fee)
		{
			auto storedFee = feeHelper->loadFee(fee->feeType, fee->asset,
				fee->accountID.get(), fee->accountType.get(), fee->subtype, fee->lowerBound, fee->upperBound, app.getDatabase(), nullptr);
			if (isDelete)
				REQUIRE(!storedFee);
			else {
				REQUIRE(storedFee);
				REQUIRE(storedFee->getFee() == *fee);
			}
		}
	}
}

[[deprecated]]
void uploadPreemissions(Application& app, SecretKey& source, SecretKey& issuance,
	Salt sourceSeq, int64 amount, AssetCode asset)
{
}

void fundAccount(Application& app, SecretKey& source, SecretKey& issuance,
    Salt& sourceSeq, BalanceID to, int64 amount, AssetCode asset)
{
}

[[deprecated]]
TransactionFramePtr createFundAccount(Hash const& networkID, SecretKey& source, SecretKey& issuance, Salt& sourceSeq, BalanceID to, int64 amount, AssetCode asset, uint32_t preemissionsPerOp, uint32_t emissionUnit, TimeBounds* timeBounds) {
	return nullptr;
}


OperationFrame const&
getFirstOperationFrame(TransactionFrame const& tx)
{
    return *(tx.getOperations()[0]);
}

OperationResult const&
getFirstResult(TransactionFrame const& tx)
{
    return getFirstOperationFrame(tx).getResult();
}

OperationResultCode
getFirstResultCode(TransactionFrame const& tx)
{
    return getFirstOperationFrame(tx).getResultCode();
}

Operation&
getFirstOperation(TransactionFrame& tx)
{
    return tx.getEnvelope().tx.operations[0];
}

void
reSignTransaction(TransactionFrame& tx, SecretKey& source)
{
    tx.getEnvelope().signatures.clear();
    tx.addSignature(source);
}

void
checkAmounts(int64_t a, int64_t b, int64_t maxd)
{
    int64_t d = b - maxd;
    REQUIRE(a >= d);
    REQUIRE(a <= b);
}

void
checkTx(int index, TxSetResultMeta& r, TransactionResultCode expected)
{
    REQUIRE(r[index].first.result.result.code() == expected);
};

void
checkTx(int index, TxSetResultMeta& r, TransactionResultCode expected,
        OperationResultCode code)
{
    checkTx(index, r, expected);
    REQUIRE(r[index].first.result.result.results()[0].code() == code);
};

}
}
