#pragma once
#include "overlay/StellarXDR.h"
#include "main/Application.h"
#include "ledger/LedgerDelta.h"
#include "database/Database.h"
#include "transactions/TransactionFrame.h"


namespace medida {
    class MetricsRegistry;
}

namespace stellar {
    namespace txtest {
        class TestManager {
        protected:
            Application &mApp;
            Database &mDB;
            LedgerDelta mDelta;
            LedgerManager &mLm;

            bool apply(TransactionFramePtr tx, std::vector<LedgerDelta::KeyEntryMap> &stateBeforeOp);

            void checkResult(TransactionResult result, bool mustSuccess);

            static Value ledgerVersion(Application& app);

            static Value externalSystemGenerators(Application& app);

        public:
            typedef std::shared_ptr<TestManager> pointer;

            TestManager(Application &app, Database &db, LedgerManager &lm);

            static pointer make(Application &app);

            static void upgradeToCurrentLedgerVersion(Application& app);

            Hash const &getNetworkID() const {
                return mApp.getNetworkID();
            }

            Database &getDB() {
                return mDB;
            }

            Application &getApp() {
                return mApp;
            }

            LedgerDelta &getLedgerDelta() {
                return mDelta;
            }

            LedgerManager &getLedgerManager() {
                return mLm;
            }

            bool applyCheck(TransactionFramePtr tx);

            bool applyCheck(TransactionFramePtr tx, std::vector<LedgerDelta::KeyEntryMap> &stateBeforeOp);
        };
    }
}
