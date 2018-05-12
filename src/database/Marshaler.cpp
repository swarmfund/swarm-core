// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "Marshaler.h"
#include "util/types.h"


namespace soci
{
using namespace stellar;
using namespace std;

void type_conversion<xdr::pointer<string64>>::from_base(
    std::string rawStr, indicator ind, xdr::pointer<string64>& result)
{
    switch (ind)
    {
    case soci::i_ok:
    {
        result = xdr::pointer<string64>(new string64(rawStr));
        return;
    }
    case soci::i_null:
        return;
    default:
        throw std::
            runtime_error("Unexpected indicator type for xdr::pointer<string64>");
    }
}

void type_conversion<xdr::pointer<string64>>::to_base(
    const xdr::pointer<string64>& raw, std::string& result, indicator& ind)
{
    if (!raw)
    {
        ind = soci::i_null;
        return;
    }

    ind = soci::i_ok;
    result = *raw;
}

void type_conversion<stellar::uint64>::from_base(unsigned long long int number,
                                                 indicator ind,
                                                 stellar::uint64& result)
{
    switch (ind)
    {
    case i_ok:
        result = number;
        break;
    case i_null:
        throw soci_error("Null value not allowed for uint64 type");
    default:
        throw std::runtime_error("Unexpected indicator type for uint64");
    }
}

void type_conversion<stellar::uint64>::to_base(const stellar::uint64& number,
                                               unsigned long long int& result,
                                               indicator& ind)
{
    ind = i_ok;
    result = number;
}

void type_conversion<PublicKey, void>::from_base(std::string raw, indicator ind,
    PublicKey& result)
{
    if (ind != i_ok)
    {
        throw std::runtime_error("Unexpected ind valud for public key");
    }

    result = StrKeyUtils::fromStrKey(raw);
}

void type_conversion<PublicKey, void>::to_base(PublicKey& raw,
    std::string& result, indicator& ind)
{
    throw std::runtime_error("It's now allowed to use `use` for any of the typedef for PublicKey. It should be converted to"
        " string manually. As it's not possible to define based on the value of PublicKey which conversion should we use");
}
}
