#pragma once

// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <soci.h>
#include <type_traits>
#include "crypto/SecretKey.h"
#include "overlay/StellarXDR.h"

using namespace stellar;

namespace soci
{

template <typename T> struct IsStringable
{
    static bool const value = std::is_same<T, AssetCode>::value || std::is_same<T, string64>::value
    || std::is_same<T, string32>::value || std::is_same<T, string256>::value || std::is_same<T, longstring>::value;
};

template <typename T> struct type_conversion<T, typename std::enable_if<
    IsStringable<T>::value>::type>
{
    typedef std::string base_type;

    static void from_base(std::string rawStr, indicator ind, T& result);

    static void to_base(const T& raw, std::string& result, indicator& ind);
};

template<typename T>
void type_conversion<T, typename std::enable_if<IsStringable<T>::value>::type>::from_base(std::string rawStr, indicator ind, T & result)
{
    switch (ind)
    {
    case i_ok:
    {
        result = rawStr;
        break;
    }
    case i_null:
        throw std::
            runtime_error("i_null is unexpected indicator value for stringable type conversion");
    case i_truncated:
        throw std::
            runtime_error("i_truncated is unexpected indicator value for stringable type conversion");
    default:
        throw std::
            runtime_error("Unexpected indicator value for stringable type conversion");
    }
}

template<typename T>
void type_conversion<T, typename std::enable_if<IsStringable<T>::value>::type>::to_base(const T & raw, std::string & result, indicator & ind)
{
    ind = i_ok;
    result = raw;
}

template <> struct type_conversion<xdr::pointer<string64>>
{
    typedef std::string base_type;

    static void from_base(std::string rawStr, indicator ind,
                          xdr::pointer<string64>& result);

    static void to_base(const xdr::pointer<string64>& raw, std::string& result,
                        indicator& ind);
};

template <> struct type_conversion<stellar::uint64>
{
    typedef unsigned long long int base_type;

    static void from_base(unsigned long long int number, indicator ind,
                          stellar::uint64& result);

    static void to_base(const stellar::uint64& number,
                        unsigned long long int& result, indicator& ind);
};

template <> struct type_conversion<ExternalSystemType>
{
    typedef int32_t base_type;

    static void from_base(int32_t number, indicator ind,
                          ExternalSystemType& result);

    static void to_base(ExternalSystemType& number, int32_t& result,
                        indicator& ind);
};

template <> struct type_conversion<PublicKey>
{
    typedef std::string base_type;

    static void from_base(std::string raw, indicator ind,
        PublicKey& result);

    static void to_base(PublicKey& raw, std::string& result,
        indicator& ind);
};

}
