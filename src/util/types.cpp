// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/types.h"
#include "lib/util/uint128_t.h"
#include <locale>
#include <algorithm>

namespace stellar
{

using xdr::operator==;

bool
isZero(uint256 const& b)
{
    for (auto i : b)
        if (i != 0)
            return false;

    return true;
}

Hash& operator^=(Hash& l, Hash const& r)
{
    std::transform(l.begin(), l.end(), r.begin(), l.begin(),
                   [](uint8_t a, uint8_t b)
                   {
                       return a ^ b;
                   });
    return l;
}

bool
lessThanXored(Hash const& l, Hash const& r, Hash const& x)
{
    Hash v1, v2;
    for (size_t i = 0; i < l.size(); i++)
    {
        v1[i] = x[i] ^ l[i];
        v2[i] = x[i] ^ r[i];
    }

    return v1 < v2;
}

uint256
makePublicKey(uint256 const& b)
{
    // SANITY pub from private
    uint256 ret;
    ret[0] = b[0];
    ret[1] = b[1];
    ret[2] = b[2];
    return (ret);
}

bool
isString32Valid(std::string const& str)
{
    for (auto c : str)
    {
        if (c < 0 || std::iscntrl(c, cLocale))
        {
            return false;
        }
    }
    return true;
}

bool isAlNum(std::string const& str) {
	for (auto c : str) {
		if (c < 0 || !std::isalnum(c, cLocale))
			return false;
	}

	return true;
}

int32_t getAnySignerType()
{
	auto allSignerTypes = xdr::xdr_traits<SignerType>::enum_values();
	int32_t result = 0;
	for (auto signerType : allSignerTypes)
	{
		result |= signerType;
	}

	return result;
}

int32 getAnyAssetPolicy()
{
	auto allAssetPolicies = xdr::xdr_traits<AssetPolicy>::enum_values();
	int32 result = 0;
	for (auto assetPolicy : allAssetPolicies)
	{
		result |= assetPolicy;
	}

	return result;
}

uint32_t getAnyBlockReason()
{
	auto allBlockReasons = xdr::xdr_traits<BlockReasons>::enum_values();
	uint32_t result = 0;
	for (auto blockReason : allBlockReasons)
	{
		result |= blockReason;
	}

	return result;
}

int32 getAnyAssetPairPolicy()
{
	auto allAssetPairPolicies = xdr::xdr_traits<AssetPairPolicy>::enum_values();
	int32 result = 0;
	for (auto assetPairPolicy : allAssetPairPolicies)
	{
		result |= assetPairPolicy;
	}

	return result;
}

bool isValidManageAssetAction(ManageAssetAction action)
{
	auto all = xdr::xdr_traits<ManageAssetAction>::enum_values();
	for (auto validAction : all)
	{
		if (validAction == static_cast<int32_t >(action))
			return true;
	}

	return false;
}

bool isValidManageAssetPairAction(ManageAssetPairAction action)
{
	auto all = xdr::xdr_traits<ManageAssetPairAction>::enum_values();
	for (auto validAction : all)
	{
		if (validAction == static_cast<int32_t >(action))
			return true;
	}

	return false;
}

std::vector<AccountType> getAllAccountTypes()
{
	auto allAccountTypes = xdr::xdr_traits<AccountType>::enum_values();
	std::vector<AccountType> result;
	for (auto rawAccountType : allAccountTypes)
	{
		result.push_back(AccountType(rawAccountType));
	}

	return result;
}

bool isSystemAccountType(AccountType accountType)
{
	for (auto systemAccountType : getSystemAccountTypes())
	{
		if (accountType == systemAccountType)
			return true;
	}

	return false;
}

std::vector<AccountType> getSystemAccountTypes()
{
	return{ AccountType::MASTER, AccountType::COMMISSION, AccountType::OPERATIONAL };
}

std::vector<SignerType> getAllSignerTypes()
{
	auto allSignerTypes = xdr::xdr_traits<SignerType>::enum_values();
	std::vector<SignerType> result;
	for (auto rawSignerType : allSignerTypes)
	{
		result.push_back(SignerType(rawSignerType));
	}

	return result;
}

std::vector<FeeType> getAllFeeTypes()
{
	auto allFeeTypes = xdr::xdr_traits<FeeType>::enum_values();
	std::vector<FeeType> result;
	for (auto rawFeeType : allFeeTypes)
	{
		result.push_back(FeeType(rawFeeType));
	}

	return result;
}

bool isFeeValid(FeeData const& fee)
{
	return fee.fixedFee >= 0 && fee.paymentFee >= 0;
}

bool isFeeTypeValid(FeeType feeType)
{
	auto allFeeTypes = xdr::xdr_traits<FeeType>::enum_values();
	for (auto rawFeeType : allFeeTypes)
	{
		if (rawFeeType == static_cast<int32_t>(feeType))
			return true;
	}

	return false;
}

int32_t getManagerType(AccountType accountType)
{
	switch (accountType)
	{
	case AccountType::NOT_VERIFIED:
		return static_cast<int32_t>(SignerType::NOT_VERIFIED_ACC_MANAGER);
	case AccountType::GENERAL:
		return static_cast<int32_t>(SignerType::GENERAL_ACC_MANAGER);
	}

	return 0;
}


// calculates A*B/C when A*B overflows 64bits
bool
bigDivide(int64_t& result, int64_t A, int64_t B, int64_t C, Rounding rounding)
{
    bool res;
    assert((A >= 0) && (B >= 0) && (C > 0));
    uint64_t r2;
    res = bigDivide(r2, (uint64_t)A, (uint64_t)B, (uint64_t)C, rounding);
    if (res)
    {
        res = r2 <= INT64_MAX;
        result = r2;
    }
    return res;
}

bool
bigDivide(uint64_t& result, uint64_t A, uint64_t B, uint64_t C, Rounding rounding)
{
    // update when moving to (signed) int128
    uint128_t a(A);
    uint128_t b(B);
    uint128_t c(C);
	uint128_t x = rounding == ROUND_DOWN ? (a * b) / c : (a * b + c - 1) / c;

    result = (uint64_t)x;

    return (x <= UINT64_MAX);
}

bool safeSum(uint64_t a, uint64_t b, uint64_t& result)
{
	result = a + b;
	return result >= a && result >= b;
}

bool safeSum(uint64_t& result, std::vector<uint64_t> params)
{
    result = 0;
    for (auto a : params)
    {
        const uint64_t b = result;
        if (!safeSum(a, b, result))
        {
            return false;
        }
    }

    return true;
}

int64_t
bigDivide(int64_t A, int64_t B, int64_t C, Rounding rounding)
{
    int64_t res;
    if (!bigDivide(res, A, B, C, rounding))
    {
        throw std::overflow_error("overflow while performing bigDivide");
    }
    return res;
}

bool
iequals(std::string const& a, std::string const& b)
{
    size_t sz = a.size();
    if (b.size() != sz)
        return false;
    for (size_t i = 0; i < sz; ++i)
        if (tolower(a[i]) != tolower(b[i]))
            return false;
    return true;
}
}
