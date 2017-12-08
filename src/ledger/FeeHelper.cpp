// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "FeeHelper.h"
#include "LedgerDelta.h"
#include "xdrpp/printer.h"

using namespace soci;
using namespace std;

namespace stellar {
    using xdr::operator<;

    const int32 EMPTY_VALUE = -1;

    static const char* feeColumnSelector = "SELECT fee_type, asset, fixed, percent, account_id, account_type, subtype, "
            "lower_bound, upper_bound, hash, lastmodified, version "
            "FROM   fee_state";

    void FeeHelper::dropAll(Database &db) {
        db.getSession() << "DROP TABLE IF EXISTS fee_state;";
        db.getSession() << "CREATE TABLE fee_state"
                "("
                "fee_type       INT          NOT NULL,"
                "asset          VARCHAR(16)  NOT NULL,"
                "fixed          BIGINT       NOT NULL,"
                "percent        BIGINT       NOT NULL,"
                "account_id     VARCHAR(56),"
                "account_type   INT,"
                "subtype        BIGINT       NOT NULL,"
                "lower_bound    BIGINT,"
                "upper_bound    BIGINT,"
                "hash           VARCHAR(256) NOT NULL,"
                "lastmodified   INT          NOT NULL,"
                "version        INT          NOT NULL   DEFAULT 0,"
                "PRIMARY KEY(hash, lower_bound, upper_bound)"
                ");";
    }

    void FeeHelper::storeAdd(LedgerDelta &delta, Database &db, LedgerEntry const &entry) {
        storeUpdateHelper(delta, db, true, entry);
    }

    void FeeHelper::storeChange(LedgerDelta &delta, Database &db, LedgerEntry const &entry) {
        storeUpdateHelper(delta, db, false, entry);
    }

    void FeeHelper::storeDelete(LedgerDelta &delta, Database &db, LedgerKey const &key) {
        auto timer = db.getDeleteTimer("fee");
        auto prep = db.getPreparedStatement("DELETE FROM fee_state WHERE hash=:hash AND lower_bound=:lb AND upper_bound=:ub");
        auto& st = prep.statement();

        string hash(binToHex(key.feeState().hash));
        st.exchange(use(hash));
        st.exchange(use(key.feeState().lowerBound));
        st.exchange(use(key.feeState().upperBound));

        st.define_and_bind();
        st.execute(true);
        delta.deleteEntry(key);
    }

    bool FeeHelper::exists(Database &db, LedgerKey const &key) {
        int exists = 0;
        auto timer = db.getSelectTimer("fee-exists");
        auto prep =
                db.getPreparedStatement("SELECT EXISTS (SELECT NULL FROM fee_state WHERE hash=:hash AND lower_bound=:lb AND upper_bound=:ub)");
        auto& st = prep.statement();
        string hash(binToHex(key.feeState().hash));
        st.exchange(use(hash));
        st.exchange(use(key.feeState().lowerBound));
        st.exchange(use(key.feeState().upperBound));

        st.exchange(into(exists));
        st.define_and_bind();
        st.execute(true);

        return exists != 0;
    }

    LedgerKey FeeHelper::getLedgerKey(LedgerEntry const &from) {
        LedgerKey ledgerKey;
        ledgerKey.type(from.data.type());
        ledgerKey.feeState().hash = from.data.feeState().hash;
        ledgerKey.feeState().lowerBound = from.data.feeState().lowerBound;
        ledgerKey.feeState().upperBound= from.data.feeState().upperBound;
        return ledgerKey;
    }

    EntryFrame::pointer FeeHelper::storeLoad(LedgerKey const &key, Database &db) {
        return loadFee(key.feeState().hash, key.feeState().lowerBound, key.feeState().upperBound, db);
    }

    EntryFrame::pointer FeeHelper::fromXDR(LedgerEntry const &from) {
        return make_shared<FeeFrame>(from);
    }

    uint64_t FeeHelper::countObjects(soci::session &sess) {
        uint64_t count = 0;
        sess << "SELECT COUNT(*) FROM fee_state;", into(count);
        return count;
    }

    void FeeHelper::storeUpdateHelper(LedgerDelta &delta, Database &db, bool insert, LedgerEntry const &entry) {

        auto feeFrame = make_shared<FeeFrame>(entry);
		auto feeEntry = feeFrame->getFee();

        feeFrame->touch(delta);

        if (!feeFrame->isValid())
        {
            CLOG(ERROR, Logging::ENTRY_LOGGER)
                    << "Unexpected state - fee is invalid: "
                    << xdr::xdr_to_string(feeEntry);
            throw std::runtime_error("Unexpected state - fee is invalid");
        }

        string sql;

        if (insert)
        {
            sql = "INSERT INTO fee_state (fee_type, asset, fixed, percent, account_id, account_type, subtype, "
                    "lastmodified, lower_bound, upper_bound, hash, version) "
                    "VALUES (:ft, :as, :f, :p, :aid, :at, :subt, :lm, :lb, :ub, :hash, :v)";
        }
        else
        {
            sql = "UPDATE fee_state "
                    "SET    fee_type=:ft, asset=:as, fixed=:f, percent=:p, account_id=:aid, "
                    "account_type=:at, subtype=:subt, lastmodified=:lm, version=:v "
                    "WHERE  lower_bound=:lb AND upper_bound=:ub AND hash=:hash";
        }

        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();

        auto feeType = static_cast<int32_t >(feeEntry.feeType);
        string assetCode = feeEntry.asset;
        st.exchange(use(feeType, "ft"));
        st.exchange(use(assetCode, "as"));
        st.exchange(use(feeEntry.fixedFee, "f"));
        st.exchange(use(feeEntry.percentFee, "p"));

        std::string actIDStrKey = "";
        if (feeEntry.accountID)
        {
            actIDStrKey = PubKeyUtils::toStrKey(*feeEntry.accountID);
        }
        st.exchange(use(actIDStrKey, "aid"));

        int32_t accountType = EMPTY_VALUE;
        if(feeEntry.accountType)
            accountType = static_cast<int32_t >(*feeEntry.accountType);
        st.exchange(use(accountType, "at"));
        st.exchange(use(feeEntry.subtype, "subt"));
        st.exchange(use(feeFrame->mEntry.lastModifiedLedgerSeq, "lm"));
        st.exchange(use(feeEntry.lowerBound, "lb"));
        st.exchange(use(feeEntry.upperBound, "ub"));

        string hash(binToHex(feeEntry.hash));
        st.exchange(use(hash, "hash"));

        auto feeVersion = static_cast<int32_t >(feeEntry.ext.v());
        st.exchange(use(feeVersion, "v"));

        st.define_and_bind();

        auto timer =
                insert ? db.getInsertTimer("fee") : db.getUpdateTimer("fee");
        st.execute(true);

        if (st.get_affected_rows() != 1)
        {
            throw std::runtime_error("could not update SQL");
        }

        if (insert)
        {
            delta.addEntry(*feeFrame);
        }
        else
        {
            delta.modEntry(*feeFrame);
        }
    }

    FeeFrame::pointer
    FeeHelper::loadFee(FeeType feeType, AssetCode asset, AccountID *accountID, AccountType *accountType,
                       int64_t subtype, int64_t lowerBound, int64_t upperBound, Database &db, LedgerDelta *delta) {
        Hash hash = FeeFrame::calcHash(feeType, asset, accountID, accountType, subtype);
        return loadFee(hash, lowerBound, upperBound, db, delta);
    }

    void FeeHelper::loadFees(StatementContext &prep, function<void(const LedgerEntry &)> feeProcessor) {
        LedgerEntry le;
        le.data.type(LedgerEntryType::FEE);

        int rawFeeType;
        string rawAsset;

        std::string actIDStrKey, rawHash;
        int32_t accountType;
        int32_t feeVersion = 0;

        auto& st = prep.statement();
        st.exchange(into(rawFeeType));
        st.exchange(into(rawAsset));
        st.exchange(into(le.data.feeState().fixedFee));
        st.exchange(into(le.data.feeState().percentFee));

        st.exchange(into(actIDStrKey));
        st.exchange(into(accountType));
        st.exchange(into(le.data.feeState().subtype));
        st.exchange(into(le.data.feeState().lowerBound));
        st.exchange(into(le.data.feeState().upperBound));
        st.exchange(into(rawHash));

        st.exchange(into(le.lastModifiedLedgerSeq));
        st.exchange(into(feeVersion));

        st.define_and_bind();
        st.execute(true);
        while (st.got_data())
        {
            le.data.feeState().asset = rawAsset;
            le.data.feeState().feeType = FeeType(rawFeeType);
            le.data.feeState().ext.v((LedgerVersion)feeVersion);
            if (!actIDStrKey.empty())
                le.data.feeState().accountID.activate() = PubKeyUtils::fromStrKey(actIDStrKey);

            if (accountType != EMPTY_VALUE)
                le.data.feeState().accountType.activate() = AccountType(accountType);

            le.data.feeState().hash = hexToBin256(rawHash);

            if (!FeeFrame::isValid(le.data.feeState()))
            {
                throw std::runtime_error("Invalid fee");
            }

            feeProcessor(le);
            st.fetch();
        }
    }

    FeeFrame::pointer
    FeeHelper::loadFee(Hash hash, int64_t lowerBound, int64_t upperBound, Database &db, LedgerDelta *delta) {
        std::string sql = feeColumnSelector;
        sql += " WHERE hash = :h AND lower_bound=:lb AND upper_bound=:ub";
        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();
        string strHash = binToHex(hash);
        st.exchange(use(strHash));
        st.exchange(use(lowerBound));
        st.exchange(use(upperBound));

        auto timer = db.getSelectTimer("fee");
        FeeFrame::pointer result;
        loadFees(prep, [&result](LedgerEntry const& of)
        {
            result = make_shared<FeeFrame>(of);
            assert(result->isValid());
            result->clearCached();
        });

        if (delta && result)
        {
            delta->recordEntry(*result);
        }

        return result;
    }

    std::vector<FeeFrame::pointer> FeeHelper::loadFees(Hash hash, Database &db) {
        std::vector<FeeFrame::pointer> fees;
        std::string sql = feeColumnSelector;
        sql += " WHERE hash = :h";
        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();
        string strHash(binToHex(hash));
        st.exchange(use(strHash));

        auto timer = db.getSelectTimer("fee");
        loadFees(prep, [&fees](LedgerEntry const& of)
        {
            fees.push_back(make_shared<FeeFrame>(of));
        });
        return fees;
    }

    bool FeeHelper::exists(Database &db, Hash hash, int64_t lowerBound, int64_t upperBound) {
        LedgerKey key;
        key.type(LedgerEntryType::FEE);
        key.feeState().hash = hash;
        key.feeState().lowerBound = lowerBound;
        key.feeState().upperBound = upperBound;
        return exists(db, key);
    }

    FeeFrame::pointer
    FeeHelper::loadForAccount(FeeType feeType, AssetCode asset, int64_t subtype, AccountFrame::pointer accountFrame,
                              int64_t amount, Database &db, LedgerDelta *delta) {
        if (!accountFrame)
            throw std::runtime_error("Expected accountFrame not to be nullptr");
        auto accountType = accountFrame->getAccountType();
        auto accountID = accountFrame->getID();
        Hash hash1 = FeeFrame::calcHash(feeType, asset, &accountID, nullptr, subtype);
        Hash hash2 = FeeFrame::calcHash(feeType, asset, nullptr, &accountType, subtype);
        Hash hash3 = FeeFrame::calcHash(feeType, asset, nullptr, nullptr, subtype);

        std::string sql = feeColumnSelector;
        sql += " WHERE hash IN (:h1, :h2, :h3) AND lower_bound <= :am1 AND :am2 <= upper_bound "
                " ORDER BY hash=:h4 DESC, hash=:h5 DESC, hash=:h6 DESC LIMIT 1";
        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();
        string strHash1 = binToHex(hash1);
        string strHash2 = binToHex(hash2);
        string strHash3 = binToHex(hash3);

        st.exchange(use(strHash1));
        st.exchange(use(strHash2));
        st.exchange(use(strHash3));
        st.exchange(use(amount));
        st.exchange(use(amount));
        st.exchange(use(strHash1));
        st.exchange(use(strHash2));
        st.exchange(use(strHash3));

        auto timer = db.getSelectTimer("fee");
        FeeFrame::pointer result;
        loadFees(prep, [&result](LedgerEntry const& of)
        {
            result = make_shared<FeeFrame>(of);
            assert(result->isValid());
            result->clearCached();
        });

        if (delta && result)
        {
            delta->recordEntry(*result);
        }

        return result;
    }

    bool FeeHelper::isBoundariesOverlap(Hash hash, int64_t lowerBound, int64_t upperBound, Database &db) {
        auto fees = loadFees(hash, db);
        for (FeeFrame::pointer feeFrame : fees)
        {
            auto fee = feeFrame->getFee();
            if (FeeFrame::isInRange(fee.lowerBound, fee.upperBound, lowerBound)
                || FeeFrame::isInRange(fee.lowerBound, fee.upperBound, upperBound))
                return true;

            if (FeeFrame::isInRange(lowerBound, upperBound, fee.lowerBound)
                || FeeFrame::isInRange(lowerBound, upperBound, fee.upperBound))
                return true;
        }
        return false;
    }

}