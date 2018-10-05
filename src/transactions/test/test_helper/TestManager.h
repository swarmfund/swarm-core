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
            Application& mApp;
            Database& mDB;
            LedgerManager& mLm;

            bool apply(TransactionFramePtr tx, std::vector<LedgerDelta::KeyEntryMap> &stateBeforeOp, LedgerDelta &txDelta);

            void checkResult(TransactionResult result, bool mustSuccess);

            static Value ledgerVersion(Application& app, LedgerVersion version);

            static Value externalSystemGenerators(Application& app);

        public:
            typedef std::shared_ptr<TestManager> pointer;

            TestManager(Application &app, Database &db, LedgerManager &lm);
            virtual ~TestManager();

            static pointer make(Application &app);

            static void upgradeToLedgerVersion(Application& app, LedgerVersion ledgerVersion);
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

            LedgerManager &getLedgerManager() {
                return mLm;
            }

            bool applyCheck(TransactionFramePtr tx);

            bool applyCheck(TransactionFramePtr tx, std::vector<LedgerDelta::KeyEntryMap> &stateBeforeOp);

            // closes an empty ledger on given time
            void advanceToTime(uint64_t closeTime);
        };
    }
}
