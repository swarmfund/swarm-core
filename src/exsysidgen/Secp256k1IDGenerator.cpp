//
// Created by User on 12/25/17.
//

#include "Secp256k1IDGenerator.h"
#include "main/Application.h"

namespace stellar {
    ExternalSystemAccountIDFrame::pointer Secp256k1IDGenerator::generateNewID(
            AccountID const& accountID, const uint64_t id)
    {
        if (id >= pow(2ull, 31))
        {
            throw std::runtime_error("Child public key index out of range. Expected value in [0, 2^31 - 1]");
        }

        auto rawPublicKey = mHdKeychain.getChildPublicKey(id);
        auto address = encodePublicKey(rawPublicKey);

        return ExternalSystemAccountIDFrame::createNew(accountID, getExternalSystemType(), address);
    }
}