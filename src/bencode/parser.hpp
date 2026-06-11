#pragma once
//pragma once = header file is only included once during compilation
//its cardinal sin to include using namespace std in header file
#include <string>
#include <vector>
#include <map>
#include <variant>
#include <string_view>

namespace bencode {

    struct BencodeValue;

    using Int = long long;
    using String = std::string;
    using List = std::vector<BencodeValue>;
    using Dict = std::map<std::string, BencodeValue>;

    struct BencodeValue {
        std::variant<Int, String, List, Dict> data;
        //variant basically stores only 1 datatype when called
        BencodeValue() = default;
        BencodeValue(Int val) : data(val) {} //creates a new struct with cariant having this integer
        BencodeValue(String val) : data(std::move(val)) {}
        BencodeValue(const char* val) : data(std::string(val)) {}
        BencodeValue(List val) : data(std::move(val)) {}
        BencodeValue(Dict val) : data(std::move(val)) {}
    };

    BencodeValue decode(std::string_view bencoded_string);
    std::string encode(const BencodeValue& value);
}