#ifndef STELLAR_HDKEYCHAIN_H
#define STELLAR_HDKEYCHAIN_H

#include <string>
#include "uchar_vector.h"
#include "hdkeys.h"


namespace stellar
{
    class HdKeychain
    {
        uchar_vector mExtendedPublicKey;

        HdKeychain(uchar_vector extendedPublicKey) {
            mExtendedPublicKey = extendedPublicKey;
        };

    public:
        HdKeychain() = default;
        static bool isValidExtendedPublicKey(const std::string &extendedPublicKey);
        static HdKeychain fromExtendedPublicKey(const std::string &extendedPublicKey);
        uchar_vector getChildPublicKey (unsigned childNumber);
    };
}

#endif //STELLAR_HDKEYCHAIN_H
