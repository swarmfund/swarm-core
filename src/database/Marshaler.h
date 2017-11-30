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
		struct type_conversion<string64>
		{
			typedef std::string base_type;

			static void from_base(std::string rawStr, indicator ind, string64& result);

			static void to_base(const string64& raw, std::string & result, indicator & ind);
		};
}