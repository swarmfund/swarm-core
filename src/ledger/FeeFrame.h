#pragma once




// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryFrame.h"
#include <functional>
#include <unordered_map>
#include <stdio.h>
#include "ledger/AccountFrame.h"

namespace soci
{
    class session;
}

namespace stellar
{
    class StatementContext;
    
    class FeeFrame : public EntryFrame{
        
        FeeEntry& mFee;
        FeeFrame(FeeFrame const& from);
    	
    public:

        static const int64_t SUBTYPE_ANY = 0;

        typedef std::shared_ptr<FeeFrame> pointer;
        
        FeeFrame();
        FeeFrame(LedgerEntry const& from);
        
        FeeFrame& operator=(FeeFrame const& other);

		static pointer create(FeeType feeType, int64_t fixedFee, int64_t percentFee, AssetCode asset, 
            AccountID* accountID = nullptr, AccountType* accountType = nullptr, int64_t subtype = SUBTYPE_ANY,
            int64_t lowerBound=0, int64_t upperBound=INT64_MAX);
        
        void storeUpdateHelper(LedgerDelta& delta, Database& db, bool insert);
        
        EntryFrame::pointer
        copy() const override
        {
            return EntryFrame::pointer(new FeeFrame(*this));
        }

		// calculates percent fee of amount with rounding up
		int64_t calculatePercentFee(int64_t amount, bool roundUp = true);

		// calculaates percent fee for period amount*percentFee*periodPassed/basePeriod
		int64_t calculatePercentFeeForPeriod(int64_t amount, int64_t periodPassed, int64_t basePeriod);
        
        FeeEntry const&
        getFee() const
        {
            return mFee;
        }
        FeeEntry&
        getFee()
        {
            return mFee;
        }

		int64_t getPercentFee()
		{
			return mFee.percentFee;
		}
        
        static bool isValid(FeeEntry const& oe);
        bool isValid() const;
        
        // Instance-based overrides of EntryFrame.
        void storeDelete(LedgerDelta& delta, Database& db) const override;
        void storeChange(LedgerDelta& delta, Database& db) override;
        void storeAdd(LedgerDelta& delta, Database& db) override;
        
        // Static helpers that don't assume an instance.
        static void storeDelete(LedgerDelta& delta, Database& db,
                                LedgerKey const& key);
        static bool exists(Database& db, LedgerKey const& key);
        static bool exists(Database& db, Hash hash, int64_t lowerBound, int64_t upperBound);
        static uint64_t countObjects(soci::session& sess);

		static bool isBoundariesOverlap(Hash hash, int64_t lowerBound, int64_t upperBound, Database& db);

		static bool isInRange(int64_t a, int64_t b, int64_t point);
        
        // database utilities
        static pointer loadFee(FeeType feeType, AssetCode asset, AccountID* accountID,
            AccountType* accountType, int64_t subtype, int64_t lowerBound, int64_t upperBound, Database& db, LedgerDelta* delta = nullptr);

		static std::vector<pointer> loadFees(Hash hash, Database& db);

        static pointer loadFee(Hash hash, int64_t lowerBound, int64_t upperBound, Database& db, LedgerDelta* delta = nullptr);

        static pointer loadForAccount(FeeType feeType, AssetCode asset, int64_t subtype, AccountFrame::pointer accountFrame, int64_t amount,
            Database& db, LedgerDelta* delta = nullptr);

        static Hash calcHash(FeeType feeType, AssetCode asset, AccountID* accountID, AccountType* accountType, int64_t subtype);

        static void
        loadFees(StatementContext& prep, std::function<void(LedgerEntry const&)> feeProcessor);
        
        static void dropAll(Database& db);
        static const char* kSQLCreateStatement1;
        static const char* kSQLCreateStatement2;
    };
}
