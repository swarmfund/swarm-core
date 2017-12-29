#include "ManageBalanceTestHelper.h"
#include "ledger/AssetHelper.h"
#include "ledger/BalanceHelper.h"
#include "transactions/ManageBalanceOpFrame.h"
#include "test/test_marshaler.h"

namespace stellar
{

namespace txtest
{
	ManageBalanceTestHelper::ManageBalanceTestHelper(TestManager::pointer testManager) : TxHelper(testManager)
	{
	}

	TransactionFramePtr 
	ManageBalanceTestHelper::createManageBalanceTx(Account& from, PublicKey& account, AssetCode asset,
												   ManageBalanceAction action, Account* signer)
	{
		Operation op;
		op.body.type(OperationType::MANAGE_BALANCE);
		auto& manageBalance = op.body.manageBalanceOp();
		manageBalance.destination = account;
		manageBalance.asset = asset;
		manageBalance.action = action;

		return TxHelper::txFromOperation(from, op, signer);
	}

	ManageBalanceResult
	ManageBalanceTestHelper::applyManageBalanceTx(Account& from, PublicKey& account, AssetCode asset,
												  ManageBalanceAction action,  
												  ManageBalanceResultCode expectedResultCode, 
												  Account* signer)
	{
		TransactionFramePtr txFrame;

		auto assetHelper = AssetHelper::Instance();
		auto balanceHelper = BalanceHelper::Instance();

		std::vector<BalanceFrame::pointer> balances;
		Database& db = mTestManager->getDB();
		balanceHelper->loadBalances(account, balances, db);

		txFrame = createManageBalanceTx(from, account, asset, action, signer);

		mTestManager->applyCheck(txFrame);

		auto txResult = txFrame->getResult();
		auto actualResultCode = 
			ManageBalanceOpFrame::getInnerCode(txResult.result.results()[0]);

		REQUIRE(actualResultCode == expectedResultCode);
		
		auto txFee = mTestManager->getApp().getLedgerManager().getTxFee();
		REQUIRE(txResult.feeCharged == txFee);

		std::vector<BalanceFrame::pointer> balancesAfter;
		balanceHelper->loadBalances(account, balancesAfter, db);

		auto opResult = txResult.result.results()[0].tr().manageBalanceResult();

		if (actualResultCode != ManageBalanceResultCode::SUCCESS)
		{
			REQUIRE(balances.size() == balancesAfter.size());
		}
		else
		{
			if (action == ManageBalanceAction::CREATE)
			{
				auto assetFrame = assetHelper->loadAsset(asset, db);
				REQUIRE(balances.size() == balancesAfter.size() - 1);
				auto balance = balanceHelper->loadBalance(opResult.success().balanceID, db);
				REQUIRE(balance);
				REQUIRE(balance->getBalance().accountID == account);
				REQUIRE(balance->getBalance().amount == 0);
				REQUIRE(balance->getBalance().asset == asset);
			}
			else
			{
				REQUIRE(balances.size() == balancesAfter.size() + 1);
				REQUIRE(!balanceHelper->loadBalance(opResult.success().balanceID, db));
			}
		}

		return opResult;
	}

	void 
	ManageBalanceTestHelper::createBalance(Account& from, PublicKey& account, AssetCode asset)
	{
		applyManageBalanceTx(from, account, asset);
	}

}

}