#ifndef STELLAR_TEST_MARSHALER_H
#define STELLAR_TEST_MARSHALER_H

#include <xdrpp/printer.h>
namespace stellar {
    template<typename T>
    inline typename std::enable_if<xdr::xdr_traits<T>::valid, std::ostream &>::type
    operator<<(std::ostream &os, const T &t) {
        return os << xdr::xdr_to_string(t);
    }
}

#include "catch.hpp"

#define GTEST_DONT_DEFINE_TEST 1
#define GTEST_DONT_DEFINE_FAIL 1
#define GTEST_DONT_DEFINE_SUCCEED 1
#define GTEST_DONT_DEFINE_ASSERT_EQ 1
#define GTEST_DONT_DEFINE_ASSERT_NE 1
#define GTEST_DONT_DEFINE_ASSERT_LE 1
#define GTEST_DONT_DEFINE_ASSERT_LT 1
#define GTEST_DONT_DEFINE_ASSERT_GE 1
#define GTEST_DONT_DEFINE_ASSERT_GT 1
#include <gmock/gmock.h>

#endif // STELLAR_TEST_MARSHALER_H
