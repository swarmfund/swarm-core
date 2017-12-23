//
// Created by kirill on 23.12.17.
//

#ifndef STELLAR_TESTUTILS_H
#define STELLAR_TESTUTILS_H

#include <lib/catch.hpp>
#include <util/types.h>

template <typename T>
void mustEqualsResultCode(T expect, T actual) {
    if(expect == actual) {
        return;
    }
    std::string firstCodeName = getCodeName<T>(expect);
    std::string secondCodeName = getCodeName<T>(expect);
    REQUIRE(firstCodeName == secondCodeName);
}

#endif //STELLAR_TESTUTILS_H
