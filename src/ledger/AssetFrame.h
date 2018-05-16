#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "ledger/EntryFrame.h"
#include "ledger/LedgerManager.h"
#include <functional>
#include <unordered_map>

namespace soci
{
class session;
}

namespace stellar
{
class StatementContext;

class AssetFrame : public EntryFrame
{
    AssetEntry& mAsset;

    AssetFrame(AssetFrame const& from);

public:
    typedef std::shared_ptr<AssetFrame> pointer;

    AssetFrame();
    AssetFrame(LedgerEntry const& from);

    AssetFrame& operator=(AssetFrame const& other);

    static pointer create(AssetCreationRequest const& request,
                          AccountID const& owner);

    EntryFrame::pointer copy() const override
    {
        return EntryFrame::pointer(new AssetFrame(*this));
    }

    AssetEntry const& getAsset() const
    {
        return mAsset;
    }

    AssetEntry& getAsset()
    {
        return mAsset;
    }

    int32 getPolicies()
    {
        return mAsset.policies;
    }

    AssetCode getCode()
    {
        return mAsset.code;
    }

    AccountID const& getOwner() const
    {
        return mAsset.owner;
    }

    uint64_t getAvailableForIssuance() const
    {
        return mAsset.availableForIssueance;
    }

    uint64_t getIssued() const
    {
        return mAsset.issued;
    }

    uint64_t getMaxIssuanceAmount() const
    {
        return mAsset.maxIssuanceAmount;
    }

    uint64_t getPendingIssuance() const;

    AccountID const& getPreIssuedAssetSigner() const
    {
        return mAsset.preissuedAssetSigner;
    }

    // returns true, if maxIssuanceAmount will be exceeded after issuance of specified amount
    bool willExceedMaxIssuanceAmount(uint64_t amount) const;
    // returns true, if available for issuance amount is sufficient to issue specified amount
    bool isAvailableForIssuanceAmountSufficient(const uint64_t amount) const
    {
        return mAsset.availableForIssueance >= amount;
    }

    // returns true, if specified amount was successfully issued
    bool tryIssue(uint64_t amount);
    // returns true, if specified amount can be authorzied to be issued in the future
    bool canAddAvailableForIssuance(uint64_t amount);
    // returns true, if specified amount can be authorized to be issued
    bool tryAddAvailableForIssuance(uint64_t amount);
    // returns true, if able to withdrawl specified amount
    bool tryWithdraw(uint64_t amount);
    // returns true, if able to lock issued amount
    bool lockIssuedAmount(uint64_t amount);
    // throws exception if fails to unlock specified amount
    void mustUnlockIssuedAmount(uint64_t const amount);

    void setPolicies(int32 policies)
    {
        mAsset.policies = policies;
    }

    void setMaxIssuance(const uint64_t amount)
    {
        mAsset.maxIssuanceAmount = amount;
    }

    void setAvailableForIssuance(const uint64_t availableForIssuance)
    {
        mAsset.availableForIssueance = availableForIssuance;
    }

    bool isPolicySet(const AssetPolicy policy) const
    {
        return isSetFlag(mAsset.policies, policy);
    }

    bool isRequireKYC() const
    {
        return isPolicySet(AssetPolicy::REQUIRES_KYC);
    }

    static bool isAssetCodeValid(AssetCode const& code);
    
    static void ensureValid(AssetEntry const& oe);
    void ensureValid() const;
};
}
