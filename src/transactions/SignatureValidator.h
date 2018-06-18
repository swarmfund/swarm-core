#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/OperationFrame.h"
#include "transactions/SourceDetails.h"

namespace stellar
{

class SignatureValidator
{
public:
	enum Result { NOT_ENOUGH_WEIGHT, INVALID_ACCOUNT_TYPE, ACCOUNT_BLOCKED, SUCCESS, INVALID_SIGNER_TYPE, EXTRA };
protected:
	std::vector<bool> mUsedSignatures;
	Hash mContentHash;
	xdr::xvector<DecoratedSignature, 20> mSignatures;

	static bool isAccountTypeAllowed(AccountFrame& account, std::vector<AccountType> allowedAccountTypes);

	static std::vector<Signer> getSigners(Application& app, Database& db, AccountFrame& account);

    Result checkSignature(Application &app, Database &db, AccountFrame &account, SourceDetails &sourceDetails);

    bool shouldSkipCheck(Application &app);


public:
	typedef std::shared_ptr<SignatureValidator> pointer;
	
	SignatureValidator(Hash contentHash, xdr::xvector<DecoratedSignature, 20> signatures);
	// checks if signature is valid.
	Result check(Application& app, Database &db, AccountFrame& account, SourceDetails& sourceDetails);
        Result check(std::vector<PublicKey> keys, int signaturesRequired, LedgerVersion ledgerVersion);
	bool checkAllSignaturesUsed();
	void resetSignatureTracker();
};
}
