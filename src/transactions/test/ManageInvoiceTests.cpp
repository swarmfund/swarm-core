// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0
#include "main/Application.h"
#include "ledger/LedgerManager.h"
#include "main/Config.h"
#include "overlay/LoopbackPeer.h"
#include "util/make_unique.h"
#include "main/test.h"
#include "lib/catch.hpp"
#include "util/Logging.h"
#include "TxTests.h"
#include "util/Timer.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "ledger/InvoiceFrame.h"
#include "ledger/InvoiceHelper.h"
#include "ledger/ReferenceFrame.h"

#include "crypto/Hex.h"
#include "crypto/SHA.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

TEST_CASE("Manage invoice", "[dep_tx][manage_invoice]")
{
    Config const& cfg = getTestConfig(0, Config::TESTDB_POSTGRESQL);

    VirtualClock clock;
    Application::pointer appPtr = Application::create(clock, cfg);
    Application& app = *appPtr;
    app.start();
    closeLedgerOn(app, 2, 1, 7, 2014);
	int64 amount = 10000;

    // set up world
    SecretKey root = getRoot();
    Salt rootSeq = 1;

    SecretKey issuance = getIssuanceKey();
    SecretKey account = SecretKey::random();
    
    SecretKey a1 = SecretKey::random();
    applyCreateAccountTx(app, root, a1, rootSeq++, AccountType::GENERAL);

    SecretKey a2 = SecretKey::random();
    applyCreateAccountTx(app, root, a2, rootSeq++, AccountType::GENERAL);

    SecretKey a3 = SecretKey::random();
    applyCreateAccountTx(app, root, a3, rootSeq++, AccountType::GENERAL);

	auto invoiceHelper = InvoiceHelper::Instance();

    auto asset = app.getBaseAsset();
	SECTION("Malformed")
	{
        applyManageInvoice(app, a1, a2.getPublicKey(),
            a1.getPublicKey(), -100, 0,
            ManageInvoiceResultCode::MALFORMED);
        applyManageInvoice(app, a1, a2.getPublicKey(),
            a1.getPublicKey(), 0, 0,
            ManageInvoiceResultCode::MALFORMED);
        applyManageInvoice(app, a1, a2.getPublicKey(),
            a1.getPublicKey(), 100, 100,
            ManageInvoiceResultCode::MALFORMED);
	}
    SECTION("WRONG BALANCE")
    {
        applyManageInvoice(app, a1, a2.getPublicKey(),
            SecretKey::random().getPublicKey(), 100, 0,
            ManageInvoiceResultCode::BALANCE_NOT_FOUND);
    }
    SECTION("Created not required review")
    {
        auto amount = 100;
        auto result = applyManageInvoice(app, a1, a2.getPublicKey(),
            a1.getPublicKey(), amount, 0);
        auto invoiceID = result.success().invoiceID;
        
        InvoiceReference invoiceReference;
        invoiceReference.invoiceID = invoiceID;
        invoiceReference.accept = true;
        
        SECTION("Deleted")
        {
            applyManageInvoice(app, a1, a2.getPublicKey(),
                a1.getPublicKey(), 0, invoiceID);
        }
        SECTION("Random invoice ID not fulfilled")
        {
            invoiceReference.invoiceID = 123;
            applyPaymentTx(app, a2, a1, rootSeq++, amount, getNoPaymentFee(), false, "", "", PaymentResultCode::INVOICE_NOT_FOUND, &invoiceReference);
        }
        SECTION("Not equal amount paid for invoice")
        {
            applyPaymentTx(app, a2, a1, rootSeq++, amount * 2, getNoPaymentFee(), false, "", "", PaymentResultCode::INVOICE_WRONG_AMOUNT, &invoiceReference);
        }
        SECTION("real invoice ID fulfilling by wrong account")
        {
            applyPaymentTx(app, a3, a1, rootSeq++, amount, getNoPaymentFee(), false, "", "", PaymentResultCode::INVOICE_ACCOUNT_MISMATCH, &invoiceReference);
        }
        SECTION("real invoice ID fulfilling for wrong balance")
        {
            applyPaymentTx(app, a2, a3, rootSeq++, amount, getNoPaymentFee(), false, "", "", PaymentResultCode::INVOICE_BALANCE_MISMATCH, &invoiceReference);
        }
        SECTION("Paid")
        {
            auto invoiceFrame = invoiceHelper->loadInvoice(invoiceID, app.getDatabase());
            REQUIRE(invoiceFrame);
            REQUIRE(invoiceFrame->getState() == InvoiceState::INVOICE_NEEDS_PAYMENT);
            auto emissionAmount = 100 * ONE;
            fundAccount(app, root, issuance, rootSeq, a2.getPublicKey(), emissionAmount);
            applyPaymentTx(app, a2, a1, rootSeq++, amount, getNoPaymentFee(), false, "", "", PaymentResultCode::SUCCESS, &invoiceReference);
            REQUIRE(!invoiceHelper->loadInvoice(invoiceID, app.getDatabase()));
        }
        SECTION("rejected")
        {
            REQUIRE(invoiceHelper->loadInvoice(invoiceID, app.getDatabase()));
            invoiceReference.accept = false;
            applyPaymentTx(app, a2, a1, rootSeq++, amount, getNoPaymentFee(), false, "", "", PaymentResultCode::SUCCESS, &invoiceReference);
            REQUIRE(!invoiceHelper->loadInvoice(invoiceID, app.getDatabase()));
        }
		SECTION("unverified")
		{
			auto notverifiedKP = SecretKey::random();
			applyCreateAccountTx(app, root, notverifiedKP, 0, AccountType::NOT_VERIFIED);
			auto result = applyManageInvoice(app, a1, notverifiedKP.getPublicKey(),
				a1.getPublicKey(), amount, 0);
			auto invoiceID = result.success().invoiceID;

			InvoiceReference invoiceReference;
			invoiceReference.invoiceID = invoiceID;
			invoiceReference.accept = false;
			SECTION("Can reject")
			{
				applyPaymentTx(app, notverifiedKP, a1, 0, amount, getNoPaymentFee(), false, "", "", PaymentResultCode::SUCCESS, &invoiceReference);
			}
			SECTION("Can't pay")
			{
				invoiceReference.accept = true;
				auto paymentTxPtr = createPaymentTx(app.getNetworkID(), notverifiedKP, a1, 0, amount, getNoPaymentFee(), false, "", "", nullptr, &invoiceReference);
				LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(),
					app.getDatabase());
				REQUIRE(!applyCheck(paymentTxPtr, delta, app));
				auto opResultCode = paymentTxPtr->getResult().result.results()[0].code();
				REQUIRE(opResultCode == OperationResultCode::opNOT_ALLOWED);
			}
		}
    }
    SECTION("Can not delete if not found")
    {
        applyManageInvoice(app, a1, a2.getPublicKey(),
            a1.getPublicKey(), 0, 88, ManageInvoiceResultCode::NOT_FOUND);
    }
    SECTION("Can not go over invoice limits")
    {
        for (int i = 0; i < app.getMaxInvoicesForReceiverAccount(); i++)
        {
            applyManageInvoice(app, a1, a2.getPublicKey(),
                a1.getPublicKey(), 100, 0);
        }
        applyManageInvoice(app, a1, a2.getPublicKey(),
            a1.getPublicKey(), 100, 0, ManageInvoiceResultCode::TOO_MANY_INVOICES);
    }
}
