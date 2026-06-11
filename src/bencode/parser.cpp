#include "parser.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <cctype>
using namespace std;

namespace bencode{

    BencodeValue parse_value(const string& s, int& p);

    BencodeValue parse_int(const string& s, int& p){
        p++;
        int start = p;
        while(p < s.size() && s[p] != 'e'){
            p++;
        }
        
        if(p == s.size()){
            throw runtime_error("Bencode syntax error: unterminated integer");
        }

        long long num = stoll(s.substr(start, p - start));
        p++;
        return BencodeValue(num); //returns new struct with variant having this integer
    }

    BencodeValue parse_string(const string& s, int& p){
        int start = p;
        while (p < s.size() && s[p] != ':'){
            p++;
        }
        
        if(p == s.size()){
            throw runtime_error("Bencode syntax error: missing colon marker");
        }

        long long len = stoll(s.substr(start, p - start));
        p++;
        
        string res = s.substr(p, len);
        p += len;
        return BencodeValue(res);
    }
    BencodeValue parse_list(const string& s, int& p){
        p++;
        List lst;
        
        while(p < s.size() && s[p] != 'e'){
            lst.push_back(parse_value(s, p));
        }
        
        if(p == s.size()){
            throw runtime_error("Bencode syntax error: unterminated list");
        }
        p++;
        
        return BencodeValue(lst);
    }

    BencodeValue parse_dict(const string& s, int& p){
        p++;
        Dict d;
        while(p < s.size() && s[p] != 'e'){
            BencodeValue key_obj = parse_string(s, p);
            string key = get<String>(key_obj.data);            
            d[key] = parse_value(s, p);
        }
        
        if(p == s.size()){
            throw runtime_error("Bencode syntax error: unterminated dictionary");
        }
        p++;
        
        return BencodeValue(d);
    }

    BencodeValue parse_value(const string& s, int& p){
        if(p >= s.size()){
            throw runtime_error("Bencode parsing error: index pointer out of bounds");
        }
        if(s[p] == 'i') return parse_int(s, p);
        if(s[p] == 'l') return parse_list(s, p);
        if(s[p] == 'd') return parse_dict(s, p);
        if(isdigit(s[p])) return parse_string(s, p);

        throw runtime_error("Bencode parsing error: invalid character detected");
    }

    void encode_append(const BencodeValue& value, string& out){
        int type_tag = value.data.index();

        if(type_tag == 0){ 
            out += "i" + to_string(get<Int>(value.data)) + "e";
        } 
        else if(type_tag == 1){ 
            string s = get<String>(value.data);
            out += to_string(s.size()) + ":" + s;
        } 
        else if(type_tag == 2){ 
            out += "l";
            for(const auto& item : get<List>(value.data)){
                encode_append(item, out);
            }
            out += "e";
        } 
        else if(type_tag == 3){ 
            out += "d";
            for(const auto& [key, val] : get<Dict>(value.data)){
                out += to_string(key.size()) + ":" + key;
                encode_append(val, out);
            }
            out += "e";
        }
    }

    BencodeValue decode(string_view bencoded_string){
        string s(bencoded_string);
        int p = 0;
        BencodeValue parsed_data = parse_value(s, p);
        
        if(p != s.size()){
            throw runtime_error("Bencode tracking validation warning: extra trailing strings left over");
        }
        return parsed_data;
    }

    string encode(const BencodeValue& value){
        string out_buffer;
        encode_append(value, out_buffer);
        return out_buffer;
    }

}