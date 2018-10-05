// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/asio.h"
#include <set>
#include "ledger/AccountHelper.h"
#include "transactions/SignatureValidatorImpl.h"
#include "transactions/TransactionFrame.h"
#include "main/Application.h"

namespace stellar
{
using namespace std;
using xdr::operator==;

SignatureValidatorImpl::SignatureValidatorImpl(Hash contentHash,
                                       xdr::xvector<DecoratedSignature, 20>
                                       signatures)
{
    mContentHash = contentHash;
    mSignatures = signatures;

    resetSignatureTracker();
}

bool SignatureValidatorImpl::checkAllSignaturesUsed()
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

void SignatureValidatorImpl::resetSignatureTracker()
{
    mUsedSignatures = vector<bool>(mSignatures.size());
}

bool SignatureValidatorImpl::isAccountTypeAllowed(AccountFrame& account,
                                              vector<AccountType>
                                              allowedAccountTypes)
{
    if (allowedAccountTypes.size() == 1)
        return true;

    auto sourceAccountType = account.getAccountType();
    for (auto allowedAccountType : allowedAccountTypes)
    {
        if (allowedAccountType == sourceAccountType)
            return true;
    }
    return false;
}

vector<Signer> SignatureValidatorImpl::getSigners(Application& app, Database& db,
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

SignatureValidatorImpl::Result SignatureValidatorImpl::check(
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


SignatureValidatorImpl::Result SignatureValidatorImpl::checkSignature(
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

bool SignatureValidatorImpl::shouldSkipCheck(Application & app)
{
    string txIDString(binToHex(mContentHash));
    auto skipCheckFor = app.getConfig().TX_SKIP_SIG_CHECK;
    if (skipCheckFor.find(txIDString) == skipCheckFor.end()) {
        return false;
    }

    for (size_t i = 0; i < mUsedSignatures.size(); i++) {
        mUsedSignatures[i] = true;
    }

    return true;
}

SignatureValidatorImpl::Result SignatureValidatorImpl::check(
    Application& app, Database& db, AccountFrame& account,
    SourceDetails& sourceDetails)
{
    if (shouldSkipCheck(app)) {
        return SUCCESS;
    }

    if ((account.getBlockReasons() | sourceDetails.mAllowedBlockedReasons) !=
        sourceDetails.mAllowedBlockedReasons)
        return ACCOUNT_BLOCKED;

    const uint32 ledgerVersion = app.getLedgerManager().getCurrentLedgerHeader().ledgerVersion;
    if (ledgerVersion < (uint32)LedgerVersion::REPLACE_ACCOUNT_TYPES_WITH_POLICIES &&
        !isAccountTypeAllowed(account, sourceDetails.mAllowedSourceAccountTypes))
    {
        return INVALID_ACCOUNT_TYPE;
    }

    if (!sourceDetails.mSpecificSigners.empty())
    {
        return check(sourceDetails.mSpecificSigners,
                     sourceDetails.mNeeededTheshold,
                     LedgerVersion(ledgerVersion));
    }

    return checkSignature(app, db, account, sourceDetails);
}
}
