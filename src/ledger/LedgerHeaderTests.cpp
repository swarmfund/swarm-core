// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "main/Application.h"
#include "util/Timer.h"
#include "util/make_unique.h"
#include "main/test.h"
#include "lib/catch.hpp"
#include "util/Logging.h"
#include "ledger/LedgerManager.h"
#include "herder/LedgerCloseData.h"
#include "xdrpp/marshal.h"

#include "main/Config.h"

using namespace stellar;
using namespace std;

typedef std::unique_ptr<Application> appPtr;

TEST_CASE("ledgerheader", "[ledger][header]")
{

    Config cfg(getTestConfig(0, Config::TESTDB_ON_DISK_SQLITE));

    Hash saved;
    {
        VirtualClock clock;
        Application::pointer app = Application::create(clock, cfg);
        app->start();

        auto const& lcl = app->getLedgerManager().getLastClosedLedgerHeader();
        auto const& lastHash = lcl.hash;
        TxSetFramePtr txSet = make_shared<TxSetFrame>(lastHash);

        // close this ledger
        StellarValue sv(txSet->getContentsHash(), 1, emptyUpgradeSteps, StellarValue::_ext_t(LedgerVersion::EMPTY_VERSION));
        LedgerCloseData ledgerData(lcl.header.ledgerSeq + 1, txSet, sv);
        app->getLedgerManager().closeLedger(ledgerData);

        saved = app->getLedgerManager().getLastClosedLedgerHeader().hash;
    }

    SECTION("load existing ledger")
    {
        Config cfg2(cfg);
        cfg2.FORCE_SCP = false;
        VirtualClock clock2;
        Application::pointer app2 = Application::create(clock2, cfg2, false);
        app2->start();

        REQUIRE(saved ==
                app2->getLedgerManager().getLastClosedLedgerHeader().hash);
    }

    SECTION("update")
    {
        VirtualClock clock;
        Application::pointer app = Application::create(clock, cfg);
        app->start();

        auto const& lcl = app->getLedgerManager().getLastClosedLedgerHeader();
        auto const& lastHash = lcl.hash;
        TxSetFramePtr txSet = make_shared<TxSetFrame>(lastHash);

        REQUIRE(lcl.header.maxTxSetSize == 100);
        REQUIRE(lcl.header.externalSystemIDGenerators.empty());
        REQUIRE(lcl.header.txExpirationPeriod == INT64_MAX / 2);


        SECTION("max tx")
        {
            StellarValue sv(txSet->getContentsHash(), 2, emptyUpgradeSteps, StellarValue::_ext_t(LedgerVersion::EMPTY_VERSION));
            {
                LedgerUpgrade up(LedgerUpgradeType::MAX_TX_SET_SIZE);
                up.newMaxTxSetSize() = 1300;
                Value v(xdr::xdr_to_opaque(up));
                sv.upgrades.emplace_back(v.begin(), v.end());
            }

            LedgerCloseData ledgerData(lcl.header.ledgerSeq + 1, txSet, sv);
            app->getLedgerManager().closeLedger(ledgerData);

            auto& newLCL = app->getLedgerManager().getLastClosedLedgerHeader();

            REQUIRE(newLCL.header.maxTxSetSize == 1300);
        }

        SECTION("external system id generator")
        {
            StellarValue sv(txSet->getContentsHash(), 2, emptyUpgradeSteps, StellarValue::_ext_t(LedgerVersion::EMPTY_VERSION));
            {
                LedgerUpgrade up(LedgerUpgradeType::EXTERNAL_SYSTEM_ID_GENERATOR);
                up.newExternalSystemIDGenerators().push_back(ExternalSystemIDGeneratorType::ETHEREUM_BASIC);
                up.newExternalSystemIDGenerators().push_back(ExternalSystemIDGeneratorType::BITCOIN_BASIC);
                Value v(xdr::xdr_to_opaque(up));
                sv.upgrades.emplace_back(v.begin(), v.end());
            }

            LedgerCloseData ledgerData(lcl.header.ledgerSeq + 1, txSet, sv);
            app->getLedgerManager().closeLedger(ledgerData);

            auto& newLCL = app->getLedgerManager().getLastClosedLedgerHeader();

            REQUIRE(newLCL.header.externalSystemIDGenerators.size() == 2);
            REQUIRE(newLCL.header.externalSystemIDGenerators[0] == ExternalSystemIDGeneratorType::ETHEREUM_BASIC);
            REQUIRE(newLCL.header.externalSystemIDGenerators[1] == ExternalSystemIDGeneratorType::BITCOIN_BASIC);
        }

        SECTION("tx expiration period")
        {
            StellarValue sv(txSet->getContentsHash(), 2, emptyUpgradeSteps, StellarValue::_ext_t(LedgerVersion::EMPTY_VERSION));
            {
                LedgerUpgrade up(LedgerUpgradeType::TX_EXPIRATION_PERIOD);
                up.newTxExpirationPeriod() = 100;
                Value v(xdr::xdr_to_opaque(up));
                sv.upgrades.emplace_back(v.begin(), v.end());
            }

            LedgerCloseData ledgerData(lcl.header.ledgerSeq + 1, txSet, sv);
            app->getLedgerManager().closeLedger(ledgerData);

            auto& newLCL = app->getLedgerManager().getLastClosedLedgerHeader();

            REQUIRE(newLCL.header.txExpirationPeriod == 100);
        }

    }
}
