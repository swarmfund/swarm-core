//
// Created by volodymyr on 13.01.18.
//
#include "util/types.h"
#include "test/test_marshaler.h"

using std::string;
using namespace stellar;

TEST_CASE("json", "[valid_json]")
{
    SECTION("empty json")
    {
        string str = "{}";
        REQUIRE(isValidJson(str));
    }
    SECTION("valid json")
    {
        string str = "{\n \"a\": \"test string\" \n}";
        REQUIRE(isValidJson(str));
    }
    SECTION("array field")
    {
        string str = "{\"array\" : [\"one\", \"two\"]}";
        REQUIRE(isValidJson(str));
    }
    SECTION("empty string")
    {
        string str = "";
        REQUIRE(!isValidJson(str));
    }
    SECTION("missed opening bracket")
    {
        string str = "\"a\" : \"test string\"}";
        REQUIRE(!isValidJson(str));
    }
    SECTION("number instead of opening bracket") {
        string str = "1 \"a\" : \"test string\"}";
        REQUIRE(!isValidJson(str));
    }
    SECTION("quotes mismatch") {
        string str = "{ a\" : \"test string\"}";
        REQUIRE(!isValidJson(str));
    }
    SECTION("missed colon")
    {
        string str = "{ \"a\" \"test string\"}";
        REQUIRE(!isValidJson(str));
    }
    SECTION("array")
    {
        string str = "{[\"one\", \"two\"]}";
        REQUIRE(!isValidJson(str));
    }
}