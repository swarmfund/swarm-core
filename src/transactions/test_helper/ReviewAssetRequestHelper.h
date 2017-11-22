#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "overlay/StellarXDR.h"
#include "transactions/test_helper/ReviewRequestHelper.h"
#include "ledger/ReviewableRequestFrame.h"

namespace stellar
{
namespace txtest 
{	
	class ReviewAssetRequestHelper : public ReviewRequestHelper
	{
		void checkApproval(AssetCreationRequest const& request, AccountID const& requestor);
		void checkApproval(AssetUpdateRequest const& request, AccountID const& requestor);
	protected:
		void checkApproval(ReviewableRequestFrame::pointer requestBeforeTx) override;
	public:
		ReviewAssetRequestHelper(TestManager::pointer testManager);
	};
}
}
