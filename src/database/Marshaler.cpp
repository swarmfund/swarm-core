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

	void type_conversion<xdr::pointer<string64>>::from_base(std::string rawStr, indicator ind, xdr::pointer<string64> & result) {
		switch (ind) {
		case soci::i_ok: 
		{
			result = xdr::pointer<string64>(new string64(rawStr));
			return;
		}
		case soci::i_null:
			return;
		default:
			throw std::runtime_error("Unexpected indicator type for xdr::pointer<string64>");
		}
	}

	void type_conversion<xdr::pointer<string64>>::to_base(const xdr::pointer<string64>& raw, std::string & result, indicator & ind) {
		if (!raw) {
			ind = soci::i_null;
			return;
		}

		ind = soci::i_ok;
		result = *raw;
	}


	void type_conversion<string64>::from_base(std::string rawStr, indicator ind, string64 & result) {
		switch (ind) {
		case soci::i_ok:
		{
			result = rawStr;
			return;
		}
		default:
			throw std::runtime_error("Unexpected indicator type for string64");
		}
	}

	void type_conversion<string64>::to_base(const string64& raw, std::string & result, indicator & ind) {
		ind = soci::i_ok;
		result = raw;
	}

    void type_conversion<stellar::uint64>::from_base(unsigned long long int number,
													   indicator ind, stellar::uint64 &result) {
        switch (ind) {
            case i_ok:
                result = number;
                break;
            case i_null:
				throw soci_error("Null value not allowed for uint64 type");
            default:
                throw std::runtime_error("Unexpected indicator type for uint64");
        }
    }

    void type_conversion<stellar::uint64>::to_base(const stellar::uint64& number,
													 unsigned long long int &result, indicator &ind) {
        ind = i_ok;
        result = number;
    }
}
