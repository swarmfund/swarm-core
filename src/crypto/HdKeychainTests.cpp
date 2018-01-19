#include "autocheck/autocheck.hpp"
#include "main/test.h"
#include "lib/catch.hpp"
#include "HdKeychain.h"

using namespace std;
using namespace stellar;

TEST_CASE("HD Keychain: extended public key -> child public key", "[crypto]")
{
    const string parentPublicKey = "xpub661MyMwAqRbcFW31YEwpkMuc5THy2PSt5bDMsktWQcFF8syAmRUapSCGu8ED9W6oDMSgv6Zz8idoc4a6mr8BDzTJY47LJhkJ8UB7WEGuduB";
    const string desiredChildPublicKey = "02fc9e5af0ac8d9b3cecfe2a888e2117ba3d089d8585886c9c826b6b22a98d12ea";

    auto keychain = HdKeychain::fromExtendedPublicKey(parentPublicKey);
    REQUIRE(keychain.getChildPublicKey(0).getHex() == desiredChildPublicKey);
}