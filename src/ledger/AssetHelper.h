#pragma once

#include <functional>
#include "EntryHelper.h"
#include "AssetFrame.h"

namespace soci
{
class session;
}

namespace stellar
{
class StatementContext;

class AssetHelper : public EntryHelper
{
public:

	virtual AssetFrame::pointer
	loadAsset(AssetCode assetCode) = 0;

	virtual AssetFrame::pointer
	mustLoadAsset(AssetCode assetCode) = 0;

	virtual AssetFrame::pointer
	loadAsset(AssetCode assetCode, AccountID owner) = 0;

private:

	virtual void
	loadAssets(StatementContext& prep,
			   std::function<void(LedgerEntry const&)> assetProcessor) = 0;

};
}