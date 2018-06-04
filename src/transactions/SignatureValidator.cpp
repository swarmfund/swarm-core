// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/asio.h"
#include <set>
#include "ledger/AccountHelper.h"
#include "transactions/SignatureValidator.h"
#include "transactions/TransactionFrame.h"
#include "main/Application.h"

namespace stellar
{
using namespace std;
using xdr::operator==;

SignatureValidator::SignatureValidator(Hash contentHash,
                                       xdr::xvector<DecoratedSignature, 20>
                                       signatures)
{
    mContentHash = contentHash;
    mSignatures = signatures;

    resetSignatureTracker();
}

bool SignatureValidator::checkAllSignaturesUsed()
{
    for (auto sigb : mUsedSignatures)
    {
        if (!sigb)
        {
            return false;
        }
    }
    return true;
}

void SignatureValidator::resetSignatureTracker()
{
    mUsedSignatures = vector<bool>(mSignatures.size());
}

bool SignatureValidator::isAccountTypeAllowed(AccountFrame& account,
                                              vector<AccountType>
                                              allowedAccountTypes)
{
    if (allowedAccountTypes.size() == 1 && allowedAccountTypes[0] == AccountType::ANY)
        return true;

    auto sourceAccountType = account.getAccountType();
    for (auto allowedAccountType : allowedAccountTypes)
    {
        if (allowedAccountType == sourceAccountType)
            return true;
    }
    return false;
}

vector<Signer> SignatureValidator::getSigners(Application& app, Database& db,
                                              AccountFrame& account)
{
    // system accounts use master's signers
    if (account.getAccountType() != AccountType::MASTER &&
        isSystemAccountType(account.getAccountType()))
    {
        auto accountHelper = AccountHelper::Instance();
        auto master = accountHelper->loadAccount(app.getMasterID(), db);
        assert(master);
        return getSigners(app, db, *master);
    }

    vector<Signer> signers;
    if (account.getAccount().thresholds[0])
        signers.push_back(
                          Signer(account.getID(),
                                 account.getAccount().thresholds[0],
                                 getAnySignerType(), 0, "", Signer::_ext_t{}));

    // create signer for recovery
    if (account.getAccountType() != AccountType::MASTER)
    {
        uint8_t recoveryWeight = 255;
        signers.push_back(Signer(account.getRecoveryID(), recoveryWeight,
                                 getAnySignerType(), 0, "", Signer::_ext_t{}));
    }

    auto accountSigners = account.getAccount().signers;
    signers.insert(signers.end(), accountSigners.begin(), accountSigners.end());
    return signers;
}

SignatureValidator::Result SignatureValidator::check(
    std::vector<PublicKey> keys, int signaturesRequired, LedgerVersion ledgerVersion)
{
    set<PublicKey> usedKeys;
    for (size_t i = 0; i < mSignatures.size(); i++)
    {
        auto const& sig = mSignatures[i];
        for (auto it = keys.begin(); it != keys.end(); ++it)
        {
            if (usedKeys.find(*it) != usedKeys.end())
                continue;
            if (PubKeyUtils::hasHint(*it, sig.hint) &&
                PubKeyUtils::verifySig(*it, sig.signature, mContentHash))
            {
                signaturesRequired--;
                if (ledgerVersion >= LedgerVersion::CHANGE_ASSET_ISSUER_BAD_AUTH_EXTRA_FIXED)
                {
                    mUsedSignatures[i] = true;
                }

                if (signaturesRequired == 0)
                {
                    return SUCCESS;
                }

                usedKeys.insert(*it);
            }
        }
    }
    return NOT_ENOUGH_WEIGHT;
}


SignatureValidator::Result SignatureValidator::checkSignature(
    Application& app, Database& db, AccountFrame& account,
    SourceDetails& sourceDetails)
{
    auto signers = getSigners(app, db, account);

    set<uint32> usedIdentities;

    // calculate the weight of the signatures
    int totalWeight = 0;

    for (size_t i = 0; i < mSignatures.size(); i++)
    {
        auto const& sig = mSignatures[i];

        for (auto const& signer : signers)
        {
            bool isSignatureValid = PubKeyUtils::hasHint(signer.pubKey,
                                                         sig.hint) &&
                                    PubKeyUtils::verifySig(signer.pubKey,
                                                           sig.signature,
                                                           mContentHash);
            if (!isSignatureValid)
                continue;

            if (usedIdentities.find(signer.identity) != usedIdentities.end())
                continue;

            if (!(signer.signerType & sourceDetails.mNeededSignedClass))
                return INVALID_SIGNER_TYPE;

            mUsedSignatures[i] = true;
            usedIdentities.insert(signer.identity);
            totalWeight += signer.weight;
            if (totalWeight >= sourceDetails.mNeeededTheshold)
                return SUCCESS;

            break;
        }
    }

    if (!checkAllSignaturesUsed())
        return EXTRA;

    return NOT_ENOUGH_WEIGHT;
}

SignatureValidator::Result SignatureValidator::check(
    Application& app, Database& db, AccountFrame& account,
    SourceDetails& sourceDetails)
{
    if ((account.getBlockReasons() | sourceDetails.mAllowedBlockedReasons) !=
        sourceDetails.mAllowedBlockedReasons)
        return ACCOUNT_BLOCKED;

    if (!isAccountTypeAllowed(account, sourceDetails.mAllowedSourceAccountTypes)
    )
        return INVALID_ACCOUNT_TYPE;

    if (!sourceDetails.mSpecificSigners.empty())
    {
        return check(sourceDetails.mSpecificSigners, sourceDetails.mNeeededTheshold, LedgerVersion(app.getLedgerManager().getCurrentLedgerHeader().ledgerVersion));
    }

    return checkSignature(app, db, account, sourceDetails);
}
}
