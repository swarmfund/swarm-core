#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <soci.h>
#include "crypto/SecretKey.h"
#include "overlay/StellarXDR.h"

using namespace stellar;

namespace soci
{
	template <>
	struct type_conversion<AccountID>
	{
		typedef std::string base_type;

		static void from_base(std::string rawAccountID, indicator ind, AccountID & accountID);

		static void to_base(const AccountID& accountID, std::string & result, indicator & ind);
	};

	template <>
	struct type_conversion<xdr::pointer<string64>>
	{
		typedef std::string base_type;

		static void from_base(std::string rawStr, indicator ind, xdr::pointer<string64> & result);

		static void to_base(const xdr::pointer<string64>& raw, std::string & result, indicator & ind);
	};

        template <>
        struct type_conversion<longstring>
        {
            typedef std::string base_type;

            static void from_base(std::string rawStr, indicator ind, longstring & result);

            static void to_base(const longstring& raw, std::string & result, indicator & ind);
        };

	template <>
	struct type_conversion<string64>
	{
		typedef std::string base_type;

		static void from_base(std::string rawStr, indicator ind, string64& result);

		static void to_base(const string64& raw, std::string & result, indicator & ind);
	};

	template <>
	struct type_conversion<stellar::uint64 >
	{
        typedef unsigned long long int base_type;

        static void from_base(unsigned long long int number, indicator ind, stellar::uint64& result);

        static void to_base(const stellar::uint64& number, unsigned long long int& result, indicator& ind);
	};

        template <>
        struct type_conversion<ExternalSystemType >
        {
            typedef int32_t base_type;

            static void from_base(int32_t number, indicator ind, ExternalSystemType& result);

            static void to_base(ExternalSystemType& number,int32_t& result, indicator& ind);
        };

}