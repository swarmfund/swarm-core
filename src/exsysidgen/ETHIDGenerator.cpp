// Copyright 2017 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include <CoinKey.h>
#include "ETHIDGenerator.h"
#include "main/Application.h"

namespace stellar {
    std::string ETHIDGenerator::encodePublicKey(const uchar_vector rawPublicKey) {
        CoinKey keyPair;
        keyPair.setPublicKey(rawPublicKey);

        return  keyPair.getEthereumAddress();
    }
}
