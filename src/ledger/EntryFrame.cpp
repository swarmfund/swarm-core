// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryFrame.h"
#include "LedgerManager.h"
#include "ledger/AccountFrame.h"
#include "ledger/ReferenceFrame.h"
#include "ledger/StatisticsFrame.h"
#include "ledger/AccountTypeLimitsFrame.h"
#include "ledger/AccountLimitsFrame.h"
#include "ledger/AssetFrame.h"
#include "ledger/AssetPairFrame.h"
#include "ledger/BalanceFrame.h"
#include "ledger/LedgerDelta.h"
#include "ledger/FeeFrame.h"
#include "ledger/PaymentRequestFrame.h"
#include "ledger/TrustFrame.h"
#include "ledger/OfferFrame.h"
#include "ledger/InvoiceFrame.h"
#include "ledger/ReviewableRequestFrame.h"
#include "xdrpp/printer.h"
#include "xdrpp/marshal.h"
#include "crypto/Hex.h"
#include "database/Database.h"

namespace stellar {
    using xdr::operator==;

    EntryFrame::pointer
    EntryFrame::FromXDR(LedgerEntry const &from) {
        EntryFrame::pointer res;

        switch (from.data.type()) {
            case ACCOUNT:
                res = std::make_shared<AccountFrame>(from);
                break;
            case FEE:
                res = std::make_shared<FeeFrame>(from);
                break;
            case BALANCE:
                res = std::make_shared<BalanceFrame>(from);
                break;
            case PAYMENT_REQUEST:
                res = std::make_shared<PaymentRequestFrame>(from);
                break;
            case ASSET:
                res = std::make_shared<AssetFrame>(from);
                break;
            case REFERENCE_ENTRY:
                res = std::make_shared<ReferenceFrame>(from);
                break;
            case ACCOUNT_TYPE_LIMITS:
                res = std::make_shared<AccountTypeLimitsFrame>(from);
                break;
            case STATISTICS:
                res = std::make_shared<StatisticsFrame>(from);
                break;
            case TRUST:
                res = std::make_shared<TrustFrame>(from);
                break;
            case ACCOUNT_LIMITS:
                res = std::make_shared<AccountLimitsFrame>(from);
                break;
            case ASSET_PAIR:
                res = std::make_shared<AssetPairFrame>(from);
                break;
            case OFFER_ENTRY:
                res = std::make_shared<OfferFrame>(from);
                break;
            case INVOICE:
                res = std::make_shared<InvoiceFrame>(from);
                break;
			case REVIEWABLE_REQUEST:
				res = std::make_shared<ReviewableRequestFrame>(from);
				break;
            default:
                CLOG(ERROR, Logging::ENTRY_LOGGER) << "Unexpected entry type on construct: " << from.data.type();
                throw std::runtime_error("Unexpected entry type on costruct");
        }
        return res;
    }

    EntryFrame::pointer
    EntryFrame::storeLoad(LedgerKey const &key, Database &db) {
        EntryFrame::pointer res;

        switch (key.type()) {
            case ACCOUNT: {
                res = std::static_pointer_cast<EntryFrame>(
                        AccountFrame::loadAccount(key.account().accountID, db));
                break;
            }
            case FEE: {
                auto const &fee = key.feeState();
                res = std::static_pointer_cast<EntryFrame>(
                        FeeFrame::loadFee(fee.hash, fee.lowerBound, fee.upperBound, db));
                break;
            }
            case BALANCE: {
                auto const &balance = key.balance();
                res = std::static_pointer_cast<EntryFrame>(BalanceFrame::loadBalance(balance.balanceID, db));
                break;
            }
            case PAYMENT_REQUEST: {
                auto const &request = key.paymentRequest();
                res = std::static_pointer_cast<EntryFrame>(PaymentRequestFrame::loadPaymentRequest(request.paymentID,
                                                                                                   db));
                break;
            }
            case ASSET: {
                auto const &asset = key.asset();
                res = std::static_pointer_cast<EntryFrame>(AssetFrame::loadAsset(asset.code, db));
                break;
            }
            case ACCOUNT_TYPE_LIMITS: {
                auto const &accountTypeLimits = key.accountTypeLimits();
                res = std::static_pointer_cast<EntryFrame>(
                        AccountTypeLimitsFrame::loadLimits(accountTypeLimits.accountType, db));
                break;
            }
            case STATISTICS: {
                auto const &stats = key.stats();
                res = std::static_pointer_cast<EntryFrame>(
                        StatisticsFrame::loadStatistics(stats.accountID, db));
                break;
            }
            case REFERENCE_ENTRY: {
                auto const &payment = key.reference();
                res = std::static_pointer_cast<EntryFrame>(ReferenceFrame::loadReference(payment.sender, payment.reference, db));
                break;
            }
            case TRUST: {
                auto const &trust = key.trust();
                res = std::static_pointer_cast<EntryFrame>(
                        TrustFrame::loadTrust(
                                trust.allowedAccount, trust.balanceToUse, db));
                break;
            }
            case ACCOUNT_LIMITS: {
                auto const &accountLimits = key.accountLimits();
                res = std::static_pointer_cast<EntryFrame>(
                        AccountLimitsFrame::loadLimits(accountLimits.accountID, db));
                break;
            }
            case ASSET_PAIR: {
                auto const &assetPair = key.assetPair();
                res = std::static_pointer_cast<EntryFrame>(
                        AssetPairFrame::loadAssetPair(assetPair.base, assetPair.quote, db));
                break;
            }
            case OFFER_ENTRY: {
                auto const &offer = key.offer();
                res = std::static_pointer_cast<EntryFrame>(
                        OfferFrame::loadOffer(offer.ownerID, offer.offerID, db));
                break;
            }
            case INVOICE: {
                auto const &invoice = key.invoice();
                res = std::static_pointer_cast<EntryFrame>(
                        InvoiceFrame::loadInvoice(invoice.invoiceID, db));
                break;
            }
			case REVIEWABLE_REQUEST: {
				auto const &request = key.reviewableRequest();
				res = std::static_pointer_cast<EntryFrame>(
					ReviewableRequestFrame::loadRequest(request.requestID, db));
				break;
			}
            default: {
                CLOG(ERROR, Logging::ENTRY_LOGGER) << "Unexpected entry type on load: " << key.type();
                throw std::runtime_error("Unexpected entry type on load");
            }

        }
        return res;
    }

    uint32
    EntryFrame::getLastModified() const {
        return mEntry.lastModifiedLedgerSeq;
    }

    uint32 &
    EntryFrame::getLastModified() {
        return mEntry.lastModifiedLedgerSeq;
    }

    void
    EntryFrame::touch(uint32 ledgerSeq) {
        assert(ledgerSeq != 0);
        getLastModified() = ledgerSeq;
    }

    void
    EntryFrame::touch(LedgerDelta const &delta) {
        uint32 ledgerSeq = delta.getHeader().ledgerSeq;
        if (delta.updateLastModified()) {
            touch(ledgerSeq);
        }
    }

    void
    EntryFrame::flushCachedEntry(LedgerKey const &key, Database &db) {
        auto s = binToHex(xdr::xdr_to_opaque(key));
        db.getEntryCache().erase_if_exists(s);
    }

    bool
    EntryFrame::cachedEntryExists(LedgerKey const &key, Database &db) {
        auto s = binToHex(xdr::xdr_to_opaque(key));
        return db.getEntryCache().exists(s);
    }

    std::shared_ptr<LedgerEntry const>
    EntryFrame::getCachedEntry(LedgerKey const &key, Database &db) {
        auto s = binToHex(xdr::xdr_to_opaque(key));
        return db.getEntryCache().get(s);
    }

    void
    EntryFrame::putCachedEntry(LedgerKey const &key,
                               std::shared_ptr<LedgerEntry const> p, Database &db) {
        auto s = binToHex(xdr::xdr_to_opaque(key));
        db.getEntryCache().put(s, p);
    }

    void
    EntryFrame::flushCachedEntry(Database &db) const {
        flushCachedEntry(getKey(), db);
    }

    void
    EntryFrame::putCachedEntry(Database &db) const {
        putCachedEntry(getKey(), std::make_shared<LedgerEntry const>(mEntry), db);
    }

    void
    EntryFrame::checkAgainstDatabase(LedgerEntry const &entry, Database &db) {
        auto key = LedgerEntryKey(entry);
        flushCachedEntry(key, db);
        auto const &fromDb = EntryFrame::storeLoad(key, db);
        if (!fromDb || !(fromDb->mEntry == entry)) {
            std::string s;
            s = "Inconsistent state between objects: ";
            s += !!fromDb ? xdr::xdr_to_string(fromDb->mEntry, "db") : "db: nullptr\n";
            s += xdr::xdr_to_string(entry, "live");
            throw std::runtime_error(s);
        }
    }

    EntryFrame::EntryFrame(LedgerEntryType type) : mKeyCalculated(false) {
        mEntry.data.type(type);
    }

    EntryFrame::EntryFrame(LedgerEntry const &from)
            : mKeyCalculated(false), mEntry(from) {
    }

    LedgerKey const &
    EntryFrame::getKey() const {
        if (!mKeyCalculated) {
            mKey = LedgerEntryKey(mEntry);
            mKeyCalculated = true;
        }
        return mKey;
    }

    void
    EntryFrame::storeAddOrChange(LedgerDelta &delta, Database &db) {
        if (exists(db, getKey())) {
            storeChange(delta, db);
        } else {
            storeAdd(delta, db);
        }
    }

    bool
    EntryFrame::exists(Database &db, LedgerKey const &key) {
        switch (key.type()) {
            case ACCOUNT:
                return AccountFrame::exists(db, key);
            case FEE:
                return FeeFrame::exists(db, key);
            case BALANCE:
                return BalanceFrame::exists(db, key);
            case PAYMENT_REQUEST:
                return PaymentRequestFrame::exists(db, key);
            case ASSET:
                return AssetFrame::exists(db, key);
            case REFERENCE_ENTRY:
                return ReferenceFrame::exists(db, key);
            case ACCOUNT_TYPE_LIMITS:
                return AccountTypeLimitsFrame::exists(db, key);
            case STATISTICS:
                return StatisticsFrame::exists(db, key);
            case TRUST:
                return TrustFrame::exists(db, key);
            case ACCOUNT_LIMITS:
                return AccountLimitsFrame::exists(db, key);
            case ASSET_PAIR:
                return AssetPairFrame::exists(db, key);
            case OFFER_ENTRY:
                return OfferFrame::exists(db, key);
            case INVOICE:
                return InvoiceFrame::exists(db, key);
			case REVIEWABLE_REQUEST:
				return ReviewableRequestFrame::exists(db, key);
            default: {
                CLOG(ERROR, Logging::ENTRY_LOGGER) << "Unexpected entry type on exists: " << key.type();
                throw std::runtime_error("Unexpected entry type on exists");
            }
        }
    }

    void
    EntryFrame::storeDelete(LedgerDelta &delta, Database &db, LedgerKey const &key) {
        switch (key.type()) {
            case ACCOUNT:
                AccountFrame::storeDelete(delta, db, key);
                break;
            case FEE:
                FeeFrame::storeDelete(delta, db, key);
                break;
            case BALANCE:
                BalanceFrame::storeDelete(delta, db, key);
                break;
            case PAYMENT_REQUEST:
                PaymentRequestFrame::storeDelete(delta, db, key);
                break;
            case ASSET:
                AssetFrame::storeDelete(delta, db, key);
                break;
            case ASSET_PAIR:
                AssetPairFrame::storeDelete(delta, db, key);
                break;
            case REFERENCE_ENTRY:
                ReferenceFrame::storeDelete(delta, db, key);
                break;
            case ACCOUNT_TYPE_LIMITS:
                AccountTypeLimitsFrame::storeDelete(delta, db, key);
                break;
            case STATISTICS:
                StatisticsFrame::storeDelete(delta, db, key);
                break;
            case TRUST:
                TrustFrame::storeDelete(delta, db, key);
                break;
            case ACCOUNT_LIMITS:
                AccountLimitsFrame::storeDelete(delta, db, key);
                break;
            case OFFER_ENTRY:
                OfferFrame::storeDelete(delta, db, key);
                break;
            case INVOICE:
                InvoiceFrame::storeDelete(delta, db, key);
                break;
			case REVIEWABLE_REQUEST:
				ReviewableRequestFrame::storeDelete(delta, db, key);
				break;
            default: {
                CLOG(ERROR, Logging::ENTRY_LOGGER) << "Unexpected entry type on delete: " << key.type();
                throw std::runtime_error("Unexpected entry type on delete");
            }
        }
    }

    LedgerKey
    LedgerEntryKey(LedgerEntry const &e) {
        auto &d = e.data;
        LedgerKey k;
        switch (d.type()) {

            case ACCOUNT:
                k.type(ACCOUNT);
                k.account().accountID = d.account().accountID;
                break;
            case FEE:
                k.type(FEE);
                k.feeState().hash = d.feeState().hash;
                k.feeState().lowerBound = d.feeState().lowerBound;
                k.feeState().upperBound = d.feeState().upperBound;
                break;
            case BALANCE:
                k.type(BALANCE);
                k.balance().balanceID = d.balance().balanceID;
                break;
            case PAYMENT_REQUEST:
                k.type(PAYMENT_REQUEST);
                k.paymentRequest().paymentID = d.paymentRequest().paymentID;
                break;
            case ASSET:
                k.type(ASSET);
                k.asset().code = d.asset().code;
                break;
            case REFERENCE_ENTRY:
                k.type(REFERENCE_ENTRY);
                k.reference().reference = d.reference().reference;
				k.reference().sender = d.reference().sender;
                break;
            case ACCOUNT_TYPE_LIMITS:
                k.type(ACCOUNT_TYPE_LIMITS);
                k.accountTypeLimits().accountType = d.accountTypeLimits().accountType;
                break;
            case STATISTICS:
                k.type(STATISTICS);
                k.stats().accountID = d.stats().accountID;
                break;
            case TRUST:
                k.type(TRUST);
                k.trust().allowedAccount = d.trust().allowedAccount;
                k.trust().balanceToUse = d.trust().balanceToUse;
                break;
            case ACCOUNT_LIMITS:
                k.type(ACCOUNT_LIMITS);
                k.accountLimits().accountID = d.accountLimits().accountID;
                break;
            case ASSET_PAIR:
                k.type(ASSET_PAIR);
                k.assetPair().base = d.assetPair().base;
                k.assetPair().quote = d.assetPair().quote;
                break;
            case OFFER_ENTRY:
                k.type(OFFER_ENTRY);
                k.offer().offerID = d.offer().offerID;
                k.offer().ownerID = d.offer().ownerID;
                break;
            case INVOICE:
                k.type(INVOICE);
                k.invoice().invoiceID = d.invoice().invoiceID;
                break;
			case REVIEWABLE_REQUEST:
				k.type(REVIEWABLE_REQUEST);
				k.reviewableRequest().requestID = d.reviewableRequest().requestID;
				break;
            default:
                CLOG(ERROR, Logging::ENTRY_LOGGER) << "Unexpected entry type on key create: " << d.type();
                throw std::runtime_error("Unexpected ledger entry type");
        }

        return k;
    }
}
