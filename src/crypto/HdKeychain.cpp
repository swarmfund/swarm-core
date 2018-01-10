#include "HdKeychain.h"

#include "uchar_vector.h"
#include "hdkeys.h"
#include "Base58Check.h"

namespace stellar
{
    HdKeychain HdKeychain::fromExtendedPublicKey(const std::string &extendedPublicKey) {
        uchar_vector rawExtendedPublicKey;
        if (!fromBase58Check(extendedPublicKey, rawExtendedPublicKey)) {
            throw std::runtime_error("Failed to parse the extended public key. Base58-encoded value expected.");
        }

        return HdKeychain(rawExtendedPublicKey);
    }

    bool HdKeychain::isValidExtendedPublicKey(const std::string &extendedPublicKey){
        try {
            auto keychain = fromExtendedPublicKey(extendedPublicKey);
            keychain.getChildPublicKey(0);

            return true;
        } catch (...) {
            return false;
        }
    }

    uchar_vector HdKeychain::getChildPublicKey(unsigned childNumber) {
        Coin::HDKeychain hdKeychain(mExtendedPublicKey);
        hdKeychain = hdKeychain.getChild(childNumber);

        return uchar_vector(hdKeychain.key());
    }
}