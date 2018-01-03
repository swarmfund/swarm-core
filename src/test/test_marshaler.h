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

#endif //STELLAR_TEST_MARSHALER_H
