#pragma once




// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryFrame.h"
#include <functional>
#include <unordered_map>
#include <stdio.h>
#include "ledger/AccountFrame.h"
#include "util/types.h"

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
        
        EntryFrame::pointer
        copy() const override
        {
            return EntryFrame::pointer(new FeeFrame(*this));
        }

                [[deprecated]]
		// calculates percent fee of amount with rounding up
		int64_t calculatePercentFee(int64_t amount, bool roundUp = true);

                bool calculatePercentFee(uint64_t amount, uint64_t& result, Rounding rounding) const;

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

        AssetCode getAsset() const {
            return mFee.asset;
        }

		int64_t getPercentFee()
		{
			return mFee.percentFee;
		}

        int64_t getFixedFee()
        {
            return mFee.fixedFee;
        }

        static void checkFeeType(FeeEntry const& feeEntry, FeeType feeType);

        bool isCrossAssetFee() const;

        AssetCode getFeeAsset() const;
        
        static bool isValid(FeeEntry const& oe);
        bool isValid() const;

		static bool isInRange(int64_t a, int64_t b, int64_t point);

        static Hash calcHash(FeeType feeType, AssetCode asset, AccountID* accountID, AccountType* accountType, int64_t subtype);

    };
}
