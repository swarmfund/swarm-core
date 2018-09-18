// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "simulation/LoadGenerator.h"
#include "main/Config.h"
#include "transactions/test/TxTests.h"
#include "herder/Herder.h"
#include "ledger/AccountHelper.h"
#include "ledger/LedgerManager.h"
#include "ledger/LedgerDeltaImpl.h"
#include "overlay/OverlayManager.h"
#include "util/Logging.h"
#include "util/Math.h"
#include "util/types.h"
#include "util/Timer.h"
#include "util/make_unique.h"

#include "database/Database.h"

#include "transactions/TransactionFrame.h"
#include "transactions/payment/PaymentOpFrame.h"
#include "transactions/CreateAccountOpFrame.h"

#include "xdrpp/printer.h"
#include "xdrpp/marshal.h"

#include "medida/metrics_registry.h"
#include "medida/meter.h"

#include <set>
#include <iomanip>
#include <cmath>
#include "ledger/AssetHelperLegacy.h"

namespace stellar
{

using namespace std;

static const uint64_t LOADGEN_ACCOUNT_BALANCE = 1000 * ONE;

// Units of load are is scheduled at 100ms intervals.
const uint32_t LoadGenerator::STEP_MSECS = 100;

LoadGenerator::LoadGenerator(Hash const& networkID)
    : mLastSecond(0)
{
    auto root = make_shared<AccountInfo>(0, txtest::getRoot(), 0, 0, *this);
    mAccounts.push_back(root);
}

LoadGenerator::~LoadGenerator()
{
    clear();
}


// Schedule a callback to generateLoad() STEP_MSECS miliseconds from now.
void
LoadGenerator::scheduleLoadGeneration(Application& app, uint32_t nAccounts,
                                      uint32_t nTxs, uint32_t txRate,
                                      bool autoRate)
{
    if (!mLoadTimer)
    {
        mLoadTimer = make_unique<VirtualTimer>(app.getClock());
    }

    if (app.getState() == Application::APP_SYNCED_STATE)
    {
        mLoadTimer->expires_from_now(std::chrono::milliseconds(STEP_MSECS));
        mLoadTimer->async_wait([this, &app, nAccounts, nTxs, txRate, autoRate](
            asio::error_code const& error)
                               {
                                   if (!error)
                                   {
                                       this->generateLoad(app, nAccounts, nTxs,
                                                          txRate, autoRate);
                                   }
                               });
    }
    else
    {
        CLOG(WARNING, "LoadGen")
            << "Application is not in sync, load generation inhibited.";
        mLoadTimer->expires_from_now(std::chrono::seconds(10));
        mLoadTimer->async_wait(
            [this, &app, nAccounts, nTxs, txRate, autoRate](
                asio::error_code const& error)
            {
                if (!error)
                {
                    this->scheduleLoadGeneration(app, nAccounts, nTxs, txRate,
                                                 autoRate);
                }
            });
    }
}

bool
LoadGenerator::maybeCreateAccount(uint32_t ledgerNum, vector<TxInfo>& txs)
{
    if (mAccounts.size() < 100 || rand_flip())
    {
        auto acc = createAccount(mAccounts.size(), ledgerNum);
		for (auto account : mAccounts)
		{
			if (acc->mKey.getPublicKey() == account->mKey.getPublicKey())
			{
				throw new std::runtime_error("Something is wrong with generate load accounts");
			}
		}

        txs.push_back(acc->creationTransaction());
        return true;
    }
    return false;
}

bool
maybeAdjustRate(double target, double actual, uint32_t& rate, bool increaseOk)
{
    if (actual == 0.0)
    {
        actual = 1.0;
    }
    double diff = target - actual;
    double acceptableDeviation = 0.1 * target;
    if (fabs(diff) > acceptableDeviation)
    {
        // Limit To doubling rate per adjustment period; even if it's measured
        // as having more room to accelerate, it's likely we'll get a better
        // measurement next time around, and we don't want to overshoot and
        // thrash. Measurement is pretty noisy.
        double pct = std::min(1.0, diff / actual);
        int32_t incr = static_cast<int32_t>(pct * rate);
        if (incr > 0 && !increaseOk)
        {
            return false;
        }
        CLOG(INFO, "LoadGen")
            << (incr > 0 ? "+++ Increasing" : "--- Decreasing")
            << " auto-tx target rate from " << rate << " to " << rate + incr;
        rate += incr;
        return true;
    }
    return false;
}

void
LoadGenerator::clear()
{
    mAccounts.clear();
}

// Generate one "step" worth of load (assuming 1 step per STEP_MSECS) at a
// given target number of accounts and txs, and a given target tx/s rate.
// If work remains after the current step, call scheduleLoadGeneration()
// with the remainder.
void
LoadGenerator::generateLoad(Application& app, uint32_t nAccounts, uint32_t nTxs,
                            uint32_t txRate, bool autoRate)
{
    soci::transaction sqltx(app.getDatabase().getSession());
    app.getDatabase().setCurrentTransactionReadOnly();

    if (txRate == 0)
    {
        txRate = 1;
    }

    // txRate is "per second"; we're running one "step" worth which is a
    // fraction of txRate determined by STEP_MSECS. For example if txRate
    // is 200 and STEP_MSECS is 100, then we want to do 20 tx per step.
    uint32_t txPerStep = (txRate * STEP_MSECS / 1000);

    // There is a wrinkle here though which is that the tx-apply phase might
    // well block timers for up to half the close-time; plus we'll be probably
    // not be scheduled quite as often as we want due to the time it takes to
    // run and the time the network is exchanging packets. So instead of a naive
    // calculation based _just_ on target rate and STEP_MSECS, we also adjust
    // based on how often we seem to be waking up and taking loadgen steps in
    // reality.
    auto& stepMeter =
        app.getMetrics().NewMeter({"loadgen", "step", "count"}, "step");
    stepMeter.Mark();
    auto stepsPerSecond = stepMeter.one_minute_rate();
    if (stepMeter.count() > 10 && stepsPerSecond != 0)
    {
        txPerStep = static_cast<uint32_t>(txRate / stepsPerSecond);
    }

    // If we have a very low tx rate (eg. 2/sec) then the previous division will
    // be zero and we'll never issue anything; what we need to do instead is
    // dispatch 1 tx every "few steps" (eg. every 5 steps). We do this by random
    // choice, weighted to the desired frequency.
    if (txPerStep == 0)
    {
        txPerStep = rand_uniform(0U, 1000U) < (txRate * STEP_MSECS) ? 1 : 0;
    }

    if (txPerStep > nTxs)
    {
        // We're done.
        CLOG(INFO, "LoadGen") << "Load generation complete.";
        app.getMetrics().NewMeter({"loadgen", "run", "complete"}, "run").Mark();
        clear();
    }
    else
    {
        auto& buildTimer =
            app.getMetrics().NewTimer({"loadgen", "step", "build"});
        auto& recvTimer =
            app.getMetrics().NewTimer({"loadgen", "step", "recv"});

        uint32_t ledgerNum = app.getLedgerManager().getLedgerNum();
        vector<TxInfo> txs;

        auto buildScope = buildTimer.TimeScope();
        for (uint32_t i = 0; i < txPerStep; ++i)
        {
            if (maybeCreateAccount(ledgerNum, txs))
            {
				++i;
                if (nAccounts > 0)
                {
                    nAccounts--;
                }
            }
            else
            {
                txs.push_back(createRandomTransaction(0.5, ledgerNum));
                if (nTxs > 0)
                {
                    nTxs--;
                }
            }
        }
        auto build = buildScope.Stop();

        auto recvScope = recvTimer.TimeScope();
        auto multinode = app.getOverlayManager().getPeers().size() > 1;
        for (auto& tx : txs)
        {
            if (!tx.execute(app))
            {
				CLOG(WARNING, "LoadGen") << "Did not applied tx";
				continue;
            }

			if (tx.mType == TxInfo::TX_CREATE_ACCOUNT)
			{
				mAccounts.push_back(tx.mTo);
			}
        }
        auto recv = recvScope.Stop();

        uint64_t now = static_cast<uint64_t>(
            VirtualClock::to_time_t(app.getClock().now()));
        bool secondBoundary = now != mLastSecond;

        if (autoRate && secondBoundary)
        {
            mLastSecond = now;

            // Automatic tx rate calculation involves taking the temperature
            // of the program and deciding if there's "room" to increase the
            // tx apply rate.
            auto& m = app.getMetrics();
            auto& ledgerCloseTimer = m.NewTimer({"ledger", "ledger", "close"});
            auto& ledgerAgeClosedTimer =
                m.NewTimer({"ledger", "age", "closed"});

            if (ledgerNum > 10 && ledgerCloseTimer.count() > 5)
            {
                // We consider the system "well loaded" at the point where its
                // ledger-close timer has avg duration within 10% of 2.5s
                // (or, well, "half the ledger-age target" which is 5s by
                // default).
                //
                // This is a bit arbitrary but it seems sufficient to
                // empirically differentiate "totally easy" from "starting to
                // struggle"; the system still has half the ledger-period to
                // digest incoming txs and acquire consensus. If it's over this
                // point, we reduce load; if it's under this point, we increase
                // load.
                //
                // We also decrease load (but don't increase it) based on ledger
                // age itself, directly: if the age gets above the herder's
                // timer target, we shed load accordingly because the *network*
                // (or some other component) is not reaching consensus fast
                // enough, independent of database close-speed.

                double targetAge =
                    (double)Herder::EXP_LEDGER_TIMESPAN_SECONDS.count() *
                    1000.0;
                double actualAge = ledgerAgeClosedTimer.mean();

                if (app.getConfig().ARTIFICIALLY_ACCELERATE_TIME_FOR_TESTING)
                {
                    targetAge = 1.0;
                }

                double targetLatency = targetAge / 2.0;
                double actualLatency = ledgerCloseTimer.mean();

                CLOG(INFO, "LoadGen")
                    << "Considering auto-tx adjustment, avg close time "
                    << ((uint32_t)actualLatency) << "ms, avg ledger age "
                    << ((uint32_t)actualAge) << "ms";

                if (!maybeAdjustRate(targetAge, actualAge, txRate, false))
                {
                    maybeAdjustRate(targetLatency, actualLatency, txRate, true);
                }

                if (txRate > 5000)
                {
                    CLOG(WARNING, "LoadGen")
                        << "TxRate > 5000, likely metric stutter, resetting";
                    txRate = 10;
                }

                // Unfortunately the timer reservoir size is 1028 by default and
                // we cannot adjust it here, so in order to adapt to load
                // relatively quickly, we clear it out every 5 ledgers.
                ledgerAgeClosedTimer.Clear();
                ledgerCloseTimer.Clear();
            }
        }

        // Emit a log message once per second.
        if (secondBoundary)
        {
            using namespace std::chrono;

            auto& m = app.getMetrics();
            auto& applyTx = m.NewTimer({"ledger", "transaction", "apply"});
            auto& applyOp = m.NewTimer({"transaction", "op", "apply"});

            auto step1ms = duration_cast<milliseconds>(build).count();
            auto step2ms = duration_cast<milliseconds>(recv).count();
            auto totalms = duration_cast<milliseconds>(build + recv).count();

            uint32_t etaSecs = (uint32_t)(((double)(nTxs + nAccounts)) /
                                          applyTx.one_minute_rate());
            uint32_t etaHours = etaSecs / 3600;
            uint32_t etaMins = etaSecs % 60;

            CLOG(INFO, "LoadGen")
                << "Tx/s: " << txRate << " target"
                << (autoRate ? " (auto), " : ", ") << std::setprecision(3)
                << applyTx.one_minute_rate() << "tx/"
                << applyOp.one_minute_rate() << "op actual (1m EWMA)."
                << " Pending: " << nAccounts << " acct, " << nTxs << " tx."
                << " ETA: " << etaHours << "h" << etaMins << "m";

            CLOG(DEBUG, "LoadGen") << "Step timing: " << totalms
                                   << "ms total = " << step1ms << "ms build, "
                                   << step2ms << "ms recv, "
                                   << (STEP_MSECS - totalms) << "ms spare";

            TxMetrics txm(app.getMetrics());
            txm.report();
        }

        scheduleLoadGeneration(app, nAccounts, nTxs, txRate, autoRate);
    }
}

LoadGenerator::AccountInfoPtr
LoadGenerator::createAccount(size_t i, uint32_t ledgerNum)
{
    auto accountName = "Account-" + to_string(i) + to_string(rand());
    return make_shared<AccountInfo>(
        i, txtest::getAccount(accountName.c_str()), 0,
        ledgerNum, *this);
}

vector<LoadGenerator::AccountInfoPtr>
LoadGenerator::createAccounts(size_t n)
{
    vector<AccountInfoPtr> result;
    for (size_t i = 0; i < n; i++)
    {
        auto account = createAccount(mAccounts.size());
        mAccounts.push_back(account);
        result.push_back(account);
    }
    return result;
}

vector<LoadGenerator::TxInfo>
LoadGenerator::accountCreationTransactions(size_t n)
{
    vector<TxInfo> result;
    for (auto account : createAccounts(n))
    {
        result.push_back(account->creationTransaction());
    }
    return result;
}

bool
LoadGenerator::loadAccount(Application& app, AccountInfo& account)
{
    AccountFrame::pointer ret;
	
	auto accountHelper = AccountHelper::Instance();
    ret = accountHelper->loadAccount(account.mKey.getPublicKey(),
                                    app.getDatabase());
    if (!ret)
    {
        return false;
    }
    return true;
}

bool
LoadGenerator::loadAccount(Application& app, AccountInfoPtr acc)
{
    if (acc)
    {
        return loadAccount(app, *acc);
    }
    return false;
}

bool
LoadGenerator::loadAccounts(Application& app, std::vector<AccountInfoPtr> accs)
{
    bool loaded = !accs.empty();
    for (auto a : accs)
    {
        if (!loadAccount(app, a))
        {
            loaded = false;
        }
    }
    return loaded;
}

LoadGenerator::TxInfo
LoadGenerator::createTransferTransaction(AccountInfoPtr from,
                                               AccountInfoPtr to,
                                               int64_t amount)
{
	auto txType = TxInfo::TX_TRANSFER;
	if (from == to || to == mAccounts[0]) {
		txType = TxInfo::TX_SKIP;
	}
    return TxInfo{from, to, txType, amount};
}

LoadGenerator::AccountInfoPtr
LoadGenerator::pickRandomAccount(AccountInfoPtr tryToAvoid, uint32_t ledgerNum)
{
    size_t i = mAccounts.size();
    while (i-- != 0)
    {
        auto n = rand_element(mAccounts);
        if (n != tryToAvoid)
        {
            return n;
        }
    }
    return tryToAvoid;
}

bool
acceptablePathExtension(LoadGenerator::AccountInfoPtr from, uint32_t ledgerNum,
                        std::vector<LoadGenerator::AccountInfoPtr> const& path,
                        LoadGenerator::AccountInfoPtr proposed)
{
    if (!proposed->canUseInLedger(ledgerNum) || from == proposed)
    {
        return false;
    }
    for (auto i : path)
    {
        if (i == proposed)
        {
            return false;
        }
    }
    return true;
}

LoadGenerator::TxInfo
LoadGenerator::createRandomTransaction(float alpha, uint32_t ledgerNum)
{
    auto from = pickRandomAccount(mAccounts.at(0), ledgerNum);
    auto amount = rand_uniform<int64_t>(10, 100);
    auto to = pickRandomAccount(from, ledgerNum);
    auto tx = createTransferTransaction(from, to, amount);
    tx.touchAccounts(ledgerNum);
    return tx;
}

vector<LoadGenerator::TxInfo>
LoadGenerator::createRandomTransactions(size_t n, float paretoAlpha)
{
    vector<TxInfo> result;
    for (size_t i = 0; i < n; i++)
    {
        result.push_back(createRandomTransaction(paretoAlpha));
    }
    return result;
}

//////////////////////////////////////////////////////
// AccountInfo
//////////////////////////////////////////////////////

LoadGenerator::AccountInfo::AccountInfo(size_t id, SecretKey key,
                                        int64_t balance,
                                        uint32_t lastChangedLedger,
                                        LoadGenerator& loadGen)
    : mId(id), mKey(key), mBalance(balance),
      mLastChangedLedger(lastChangedLedger), mLoadGen(loadGen)
{
}

LoadGenerator::TxInfo
LoadGenerator::AccountInfo::creationTransaction()
{
    return TxInfo{mLoadGen.mAccounts[0], shared_from_this(),
                  TxInfo::TX_CREATE_ACCOUNT, LOADGEN_ACCOUNT_BALANCE };
}

LoadGenerator::TxInfo LoadGenerator::AccountInfo::fundTransaction() {
	return TxInfo{ mLoadGen.mAccounts[0], shared_from_this(),
		TxInfo::TX_FUND_ACCOUNT, LOADGEN_ACCOUNT_BALANCE };
}

bool
LoadGenerator::AccountInfo::canUseInLedger(uint32_t currentLedger)
{
    // Leave a 3-ledger window between uses of an account, in case
    // it gets kicked down the road a bit.
    return (mLastChangedLedger + 3) < currentLedger;
}

//////////////////////////////////////////////////////
// TxInfo
//////////////////////////////////////////////////////

LoadGenerator::TxMetrics::TxMetrics(medida::MetricsRegistry& m)
    : mAccountCreated(m.NewMeter({"loadgen", "account", "created"}, "account"))
    , mPayment(m.NewMeter({"loadgen", "payment", "any"}, "payment"))

    , mTxnAttempted(m.NewMeter({"loadgen", "txn", "attempted"}, "txn"))
    , mTxnRejected(m.NewMeter({"loadgen", "txn", "rejected"}, "txn"))
    , mTxnBytes(m.NewMeter({"loadgen", "txn", "bytes"}, "txn"))
{
}

void
LoadGenerator::TxMetrics::report()
{
    CLOG(DEBUG, "LoadGen") << "Counts: " << mTxnAttempted.count() << " tx, "
                           << mTxnRejected.count() << " rj, "
                           << mTxnBytes.count() << " by, "
                           << mAccountCreated.count() << " ac ("
                           << mPayment.count();

	CLOG(DEBUG, "LoadGen") << "Rates/sec (1m EWMA): " << std::setprecision(3)
		<< mTxnAttempted.one_minute_rate() << " tx, "
		<< mTxnRejected.one_minute_rate() << " rj, "
		<< mTxnBytes.one_minute_rate() << " by, "
		<< mAccountCreated.one_minute_rate() << " ac, "
		<< mPayment.one_minute_rate();
}

void
LoadGenerator::TxInfo::touchAccounts(uint32_t ledger)
{
    if (mFrom)
    {
        mFrom->mLastChangedLedger = ledger;
    }
    if (mTo)
    {
        mTo->mLastChangedLedger = ledger;
    }
}

bool
LoadGenerator::TxInfo::execute(Application& app)
{
    std::vector<TransactionFramePtr> txfs;
    TxMetrics txm(app.getMetrics());
    toTransactionFrames(app, txfs, txm);
    for (auto f : txfs)
    {
        txm.mTxnAttempted.Mark();
        {
            StellarMessage msg;
            msg.type(MessageType::TRANSACTION);
            msg.transaction() = f->getEnvelope();
            txm.mTxnBytes.Mark(xdr::xdr_argpack_size(msg));
        }
        auto status = app.getHerder().recvTransaction(f);
        if (status != Herder::TX_STATUS_PENDING)
        {

            static const char* TX_STATUS_STRING[Herder::TX_STATUS_COUNT] = {
                "PENDING", "DUPLICATE", "ERROR"};

            CLOG(INFO, "LoadGen")
                << "tx rejected '" << TX_STATUS_STRING[status]
                << "': " << xdr::xdr_to_string(f->getEnvelope()) << " ===> "
                << xdr::xdr_to_string(f->getResult());
            txm.mTxnRejected.Mark();
            return false;
        }
    }
    recordExecution(app.getConfig().DESIRED_BASE_FEE);
    return true;
}

void
LoadGenerator::TxInfo::toTransactionFrames(
	Application& app, std::vector<TransactionFramePtr>& txs,
    TxMetrics& txm)
{

	TimeBounds timeBounds;
	timeBounds.minTime = 0;
	timeBounds.maxTime = app.getLedgerManager().getCloseTime() + 60*60;

        std::vector<AssetFrame::pointer> baseAssets;
        AssetHelperLegacy::Instance()->loadBaseAssets(baseAssets, app.getDatabase());
        if (baseAssets.empty())
        {
            throw std::runtime_error("Expected base assets to exist in db");
        }
        auto asset = baseAssets[0];
        const int maxOperationsPerTx = 100;
    switch (mType)
    {
    case TxInfo::TX_CREATE_ACCOUNT:
        txm.mAccountCreated.Mark();
        {

            auto resultingEnvelope = txtest::createCreateAccountTx(app.getNetworkID(), mFrom->mKey, mTo->mKey, 1,
                AccountType::GENERAL, nullptr, &timeBounds)->getEnvelope();

			Salt seq = 1;
			auto issuenceKey = mFrom->mKey;
			auto fundAccountTx = txtest::createFundAccount(app.getNetworkID(), mFrom->mKey, issuenceKey, seq, mTo->mKey.getPublicKey(),
				mAmount, asset->getCode(), maxOperationsPerTx, 100 * ONE, &timeBounds);
			auto ops = fundAccountTx->getEnvelope().tx.operations;
			for (int i = 0; i < ops.size(); i++)
			{
				resultingEnvelope.tx.operations.push_back(ops[ops.size() - i - 1]);
			}
			TransactionFramePtr tx =
				TransactionFrame::makeTransactionFromWire(
					app.getNetworkID(), resultingEnvelope);

			tx->getEnvelope().signatures.clear();
			tx->addSignature(mFrom->mKey);
			txs.push_back(tx);
			break;
        }
	case TxInfo::TX_FUND_ACCOUNT:
	{
		Salt seq = 1;
		auto issuenceKey = mFrom->mKey;
		auto fundAccountTx = txtest::createFundAccount(app.getNetworkID(), mFrom->mKey, issuenceKey, seq, mTo->mKey.getPublicKey(),
			mAmount, asset->getCode(), maxOperationsPerTx, 100 * ONE, &timeBounds);
		txs.push_back(fundAccountTx);
		break;
	}
    case TxInfo::TX_TRANSFER:
	{
		txm.mPayment.Mark();
		txs.push_back(txtest::createPaymentTx(app.getNetworkID(), mFrom->mKey, mTo->mKey,
			1, mAmount, txtest::getNoPaymentFee(), false, "", "", &timeBounds));
		break;
	}
	case TxInfo::TX_SKIP:
		break;
     
    default:
        assert(false);
    }
}

void
LoadGenerator::AccountInfo::createDirectly(Application& app)
{
	AccountFrame a(mKey.getPublicKey());
	auto ledger = app.getLedgerManager().getLedgerNum();
	a.touch(ledger);
	LedgerDeltaImpl delta(app.getLedgerManager().getCurrentLedgerHeader(),
		app.getDatabase());;
	EntryHelperProvider::storeAddEntry(delta, app.getDatabase(), a.mEntry);
}


void
LoadGenerator::TxInfo::recordExecution(int64_t baseFee)
{
    // nothing to record for now
}
}
