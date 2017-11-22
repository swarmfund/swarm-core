#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "overlay/StellarXDR.h"
#include "crypto/SecretKey.h"

namespace stellar
{
namespace txtest 
{
	struct Account
	{
		SecretKey key;
		Salt salt;

		Salt getNextSalt() {
			return salt++;
		}
	};
}
}
