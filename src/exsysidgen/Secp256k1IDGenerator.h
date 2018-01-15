//
// Created by User on 12/25/17.
//

#ifndef STELLAR_SECP256K1IDGENERATOR_H
#define STELLAR_SECP256K1IDGENERATOR_H

#include <hdkeys.h>
#include "exsysidgen/Generator.h"
#include "uchar_vector.h"

using namespace Coin;

namespace stellar {
    // Base class for all cryptocurrencies that utilize Secp256K1 curve
    class Secp256k1IDGenerator : public Generator {
    private:
        HDKeychain mHdKeychain;
        virtual std::string encodePublicKey(HDKeychain keychain) = 0;
    public:
        Secp256k1IDGenerator() = default;
        Secp256k1IDGenerator(Application& app, Database& db, std::string extendedPublicKey)
                : Generator(app, db)
        {
            mHdKeychain = HDKeychain::fromExtendedPublicKey(extendedPublicKey);
        }

        ExternalSystemAccountIDFrame::pointer generateNewID(
                AccountID const& accountID, uint64_t id) override;
    };
}


#endif //STELLAR_SECP256K1IDGENERATOR_H
