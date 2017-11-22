// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "Marshaler.h"


namespace soci
{

	using namespace stellar;
	using namespace std;

	void type_conversion<AccountID>::from_base(std::string rawAccountID, indicator ind, AccountID & accountID)
	{
		if (ind == i_null)
		{
			throw soci_error("Null value not allowed for AccountID type");
		}

		accountID = PubKeyUtils::fromStrKey(rawAccountID);
	}


	void type_conversion<AccountID>::to_base(const AccountID& accountID, std::string & result, indicator & ind)
	{
		result = PubKeyUtils::toStrKey(accountID);
		ind = i_ok;
	}
}
