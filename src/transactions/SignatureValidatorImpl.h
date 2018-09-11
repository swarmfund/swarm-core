#pragma once

// Copyright 2015 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "transactions/SignatureValidator.h"

namespace stellar
{

class SignatureValidatorImpl : public SignatureValidator
{
protected:
	std::vector<bool> mUsedSignatures;
	Hash mContentHash;
	xdr::xvector<DecoratedSignature, 20> mSignatures;

	static bool isAccountTypeAllowed(AccountFrame& account, std::vector<AccountType> allowedAccountTypes);

	static std::vector<Signer> getSigners(Application& app, Database& db, AccountFrame& account);

    Result checkSignature(Application &app, Database &db, AccountFrame &account, SourceDetails &sourceDetails);

    bool shouldSkipCheck(Application &app);

public:
	SignatureValidatorImpl(Hash contentHash, xdr::xvector<DecoratedSignature, 20> signatures);

	Result check(Application& app, Database &db, AccountFrame& account, SourceDetails& sourceDetails) override;
	Result check(std::vector<PublicKey> keys, int signaturesRequired, LedgerVersion ledgerVersion) override;
	bool checkAllSignaturesUsed() override;
	void resetSignatureTracker() override;
};
}
