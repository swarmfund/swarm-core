#include "ledger/FeeFrame.h"
#include "database/Database.h"
#include "crypto/SecretKey.h"
#include "crypto/SHA.h"
#include "LedgerDelta.h"
#include "util/types.h"

using namespace std;
using namespace soci;

namespace stellar
{
    const int32 EMPTY_VALUE = -1;

    const char* FeeFrame::kSQLCreateStatement1 =
    "CREATE TABLE fee_state"
    "("
    "fee_type INT    NOT NULL,"
    "asset    VARCHAR(16)       NOT NULL,"
    "fixed          BIGINT NOT NULL,"
	"percent        BIGINT NOT NULL,"
    "account_id    VARCHAR(56),"
    "account_type    INT,"
    "subtype    BIGINT NOT NULL,"
    "lower_bound    BIGINT,"
    "upper_bound    BIGINT,"
    "hash    VARCHAR(256)  NOT NULL,"
    "lastmodified   INT    NOT NULL,"
		"PRIMARY KEY(hash, lower_bound, upper_bound)"
    ");";
    
    static const char* feeColumnSelector = "SELECT fee_type, asset, fixed, percent, account_id, account_type, subtype, lower_bound, upper_bound, hash, lastmodified FROM fee_state";

    
    FeeFrame::FeeFrame() : EntryFrame(FEE), mFee(mEntry.data.feeState())
    {
    }
    
    FeeFrame::FeeFrame(LedgerEntry const& from)
    : EntryFrame(from), mFee(mEntry.data.feeState())
    {
    }
    
    FeeFrame::FeeFrame(FeeFrame const& from) : FeeFrame(from.mEntry)
    {
    }
    
    FeeFrame& FeeFrame::operator=(FeeFrame const& other)
    {
        if (&other != this)
        {
            mFee = other.mFee;
            mKey = other.mKey;
            mKeyCalculated = other.mKeyCalculated;
        }
        return *this;
    }

	FeeFrame::pointer FeeFrame::create(FeeType feeType, int64_t fixedFee,
        int64_t percentFee, AssetCode asset, AccountID* accountID,
        AccountType* accountType, int64_t subtype, int64_t lowerBound, int64_t upperBound)
	{
		LedgerEntry le;
		le.data.type(FEE);
		FeeEntry& entry = le.data.feeState();
		entry.fixedFee = fixedFee;
		entry.percentFee = percentFee;
		entry.feeType = feeType;
		entry.asset = asset;
        entry.subtype = subtype;
        if (accountID)
            entry.accountID.activate() = *accountID;

        if (accountType)
            entry.accountType.activate() = *accountType;

        entry.lowerBound = lowerBound;
        entry.upperBound = upperBound;

        entry.hash = calcHash(feeType, asset, accountID, accountType, subtype);
		return std::make_shared<FeeFrame>(le);
	}

    uint64_t
    FeeFrame::countObjects(soci::session& sess)
    {
        uint64_t count = 0;
        sess << "SELECT COUNT(*) FROM fee_state;", into(count);
        return count;
    }
    void
    FeeFrame::storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert)
    {
        touch(delta);
        
        if (!isValid())
        {
            throw std::runtime_error("Invalid fee state");
        }
        
        string sql;
        
        if (insert)
        {
            sql = "INSERT INTO fee_state (fee_type, asset, fixed, percent, account_id, account_type, subtype, lastmodified, lower_bound, upper_bound, hash) VALUES (:ft, :as, :f, :p, :aid, :at, :subt, :lm, :lb, :ub, :hash)";
        }
        else
        {
            sql = "UPDATE fee_state SET fee_type = :ft, asset = :as, fixed=:f, percent =:p, account_id=:aid, account_type=:at, subtype=:subt, lastmodified=:lm WHERE lower_bound=:lb AND upper_bound=:ub AND hash=:hash";
        }
            
        
        auto prep = db.getPreparedStatement(sql);
        auto& st = prep.statement();
        
		int feeType = mFee.feeType;
        string assetCode = mFee.asset;
        st.exchange(use(feeType, "ft"));
        st.exchange(use(assetCode, "as"));
        st.exchange(use(mFee.fixedFee, "f"));
		st.exchange(use(mFee.percentFee, "p"));

        std::string actIDStrKey = "";
        if (mFee.accountID)
        {
            actIDStrKey = PubKeyUtils::toStrKey(*mFee.accountID);
        }
        st.exchange(use(actIDStrKey, "aid"));

        int32_t accountType = EMPTY_VALUE;
        if(mFee.accountType)
            accountType = *mFee.accountType;
        st.exchange(use(accountType, "at"));

        st.exchange(use(mFee.subtype, "subt"));
		st.exchange(use(getLastModified(), "lm"));

        st.exchange(use(mFee.lowerBound, "lb"));
        st.exchange(use(mFee.upperBound, "ub"));


        string hash(binToHex(mFee.hash));
        st.exchange(use(hash, "hash"));

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
            delta.addEntry(*this);
        }
        else
        {
            delta.modEntry(*this);
        }
    }

    FeeFrame::pointer FeeFrame::loadForAccount(FeeType feeType, AssetCode asset, int64_t subtype, AccountFrame::pointer accountFrame, int64_t amount,
        Database& db, LedgerDelta* delta)
    {

		if (!accountFrame)
			throw std::runtime_error("Expected accountFrame not to be nullptr");
        auto accountType = accountFrame->getAccountType();
        auto accountID = accountFrame->getID();
        Hash hash1 = calcHash(feeType, asset, &accountID, nullptr, subtype);
        Hash hash2 = calcHash(feeType, asset, nullptr, &accountType, subtype);
        Hash hash3 = calcHash(feeType, asset, nullptr, nullptr, subtype);

		std::string sql = feeColumnSelector;
		sql += " WHERE hash IN (:h1, :h2, :h3) AND lower_bound <= :am1 AND :am2 <= upper_bound  ORDER BY hash=:h4 DESC, hash=:h5 DESC, hash=:h6 DESC LIMIT 1";
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
			result->mKeyCalculated = false;
		});

		if (delta && result)
		{
			delta->recordEntry(*result);
		}

		return result;
    }

    
    FeeFrame::pointer FeeFrame::loadFee(Hash hash, int64_t lowerBound, int64_t upperBound, Database& db, LedgerDelta* delta)
    {
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
			result->mKeyCalculated = false;
		});

		if (delta && result)
		{
			delta->recordEntry(*result);
		}

		return result;
    }


    FeeFrame::pointer FeeFrame::loadFee(FeeType feeType, AssetCode asset,
        AccountID* accountID, AccountType* accountType, int64_t subtype, int64_t lowerBound, int64_t upperBound, Database& db, LedgerDelta* delta)
    {
        Hash hash = calcHash(feeType, asset, accountID, accountType, subtype);
        return loadFee(hash, lowerBound, upperBound, db, delta);
    }

    void
    FeeFrame::loadFees(StatementContext& prep,
                       std::function<void(LedgerEntry const&)> feeProcessor)
    {

        LedgerEntry le;
        le.data.type(FEE);

		int rawFeeType;
		string rawAsset;

		std::string actIDStrKey, rawHash;
		int32_t accountType;

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

        st.define_and_bind();
        st.execute(true);
        while (st.got_data())
        {   
            le.data.feeState().asset = rawAsset;
            le.data.feeState().feeType = FeeType(rawFeeType);
			if (actIDStrKey.size() > 0)
				le.data.feeState().accountID.activate() = PubKeyUtils::fromStrKey(actIDStrKey);

			if (accountType != EMPTY_VALUE)
				le.data.feeState().accountType.activate() = AccountType(accountType);

			le.data.feeState().hash = hexToBin256(rawHash);

            if (!isValid(le.data.feeState()))
            {
                throw std::runtime_error("Invalid fee");
            }

            feeProcessor(le);
            st.fetch();
        }
    }

    std::vector<FeeFrame::pointer>
    FeeFrame::loadFees(Hash hash, Database& db)
    {
		std::vector<pointer> fees;
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


    bool
    FeeFrame::exists(Database& db, LedgerKey const& key)
    {
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
    
    bool FeeFrame::exists(Database& db, Hash hash, int64_t lowerBound, int64_t upperBound)
    {
		LedgerKey key;
		key.type(FEE);
		key.feeState().hash = hash;
		key.feeState().lowerBound = lowerBound;
		key.feeState().upperBound = upperBound;
		return exists(db, key);
    }

	bool FeeFrame::isInRange(int64_t a, int64_t b, int64_t point)
	{
		return a <= point && point <= b;
	}

	bool FeeFrame::isBoundariesOverlap(Hash hash, int64_t lowerBound, int64_t upperBound, Database& db)
	{
		auto fees = loadFees(hash, db);
		for (pointer feeFrame : fees)
		{
			auto fee = feeFrame->getFee();
			if (isInRange(fee.lowerBound, fee.upperBound, lowerBound) || isInRange(fee.lowerBound, fee.upperBound, upperBound))
				return true;

			if (isInRange(lowerBound, upperBound, fee.lowerBound) || isInRange(lowerBound, upperBound, fee.upperBound))
				return true;
		}
		return false;
	}

    void
    FeeFrame::storeDelete(LedgerDelta& delta, Database& db) const
    {
        storeDelete(delta, db, getKey());
    }
    
    void FeeFrame::storeDelete(LedgerDelta& delta, Database& db, LedgerKey const& key)
    {
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
    void
    FeeFrame::storeChange(LedgerDelta& delta, Database& db)
    {
        storeUpdateHelper(delta, db, false);
    }
    
    void
    FeeFrame::storeAdd(LedgerDelta& delta, Database& db)
    {
        storeUpdateHelper(delta, db, true);
    }

	int64_t FeeFrame::calculatePercentFee(int64_t amount, bool roundUp)
	{
		if (mFee.percentFee == 0)
			return 0;
		auto rounding = roundUp ? ROUND_UP : ROUND_DOWN;
		return bigDivide(amount, mFee.percentFee, 100 * ONE, rounding);
	}

	int64_t FeeFrame::calculatePercentFeeForPeriod(int64_t amount, int64_t periodPassed, int64_t basePeriod)
	{
		if (mFee.percentFee == 0 || periodPassed == 0 || basePeriod == 0 || amount == 0)
		{
			return 0;
		}

		int64_t percentFeeForFullPeriod = calculatePercentFee(amount);
		return bigDivide(percentFeeForFullPeriod, periodPassed, basePeriod, ROUND_UP);
	}

    
    bool
    FeeFrame::isValid(FeeEntry const& oe)
    {
        auto res = oe.fixedFee >= 0 && oe.percentFee >= 0 && oe.percentFee <= 100 * ONE && isFeeTypeValid(oe.feeType) && oe.lowerBound <= oe.upperBound;
		return res;
    }

    Hash
    FeeFrame::calcHash(FeeType feeType, AssetCode asset, AccountID* accountID, AccountType* accountType, int64_t subtype)
    {
        std::string data = "";

        char buff[100];
        snprintf(buff, sizeof(buff), "type:%i", feeType);
        std::string buffAsStdStr = buff;
        data += buffAsStdStr;

        std::string rawAsset = asset;
        snprintf(buff, sizeof(buff), "asset:%s", rawAsset.c_str());
        buffAsStdStr = buff;
        data += buffAsStdStr;

        snprintf(buff, sizeof(buff), "subtype:%s", std::to_string(subtype).c_str());
        buffAsStdStr = buff;
        data += buffAsStdStr;

        if (accountID) {
            std::string actIDStrKey = PubKeyUtils::toStrKey(*accountID);
            snprintf(buff, sizeof(buff), "accountID:%s", actIDStrKey.c_str());
            buffAsStdStr = buff;
            data += buffAsStdStr;
        }
        if (accountType) {
            snprintf(buff, sizeof(buff), "accountType:%i", *accountType);
            buffAsStdStr = buff;
            data += buffAsStdStr;
        }
        
        return Hash(sha256(data));
    }

    bool
    FeeFrame::isValid() const
    {
        return isValid(mFee);
    }
    
    void
    FeeFrame::dropAll(Database& db)
    {
        db.getSession() << "DROP TABLE IF EXISTS fee_state;";
        db.getSession() << kSQLCreateStatement1;
    }
    
}
