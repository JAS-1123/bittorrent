#include "torrent_file.hpp"
#include "bencode/parser.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <openssl/sha.h>
using namespace std;
namespace bencode{

    static string read_file(const string& path){
        ifstream f(path, ios::binary);
        if (!f.is_open()) throw runtime_error("TorrentFile: cannot open file: " + path);
        return string(istreambuf_iterator<char>(f), {});
        //reads file, binary is there to read bit by bit
    }
    static string sha1_raw(const string& data){
        unsigned char digest[20];
        SHA1(reinterpret_cast<const unsigned char*>(data.c_str()), data.size(), digest);
        return string(reinterpret_cast<char*>(digest), 20);
        //decrypts sha-1 via openssl algo
    }

static string extract_info_raw(const string& raw){
        size_t pos = raw.find("4:info");
        if (pos == string::npos) throw runtime_error("TorrentFile: could not locate 4:info key in torrent");

        size_t start = pos + 6;
        if (start >= raw.size() || raw[start] != 'd') {
            throw runtime_error("TorrentFile: info key does not point to a valid dictionary");
        }

        size_t i = start;
        int depth = 0;

        while(i < raw.size()){
            char ch = raw[i];
            if (ch == 'd' || ch == 'l'){
                depth++;
                i++;
            } 
            else if(ch == 'e'){
                depth--;
                i++;
                if(depth == 0){
                    return raw.substr(start, i - start);
                }
            } 
            else if(ch >= '0' && ch <= '9'){
                size_t colon = raw.find(':', i);
                if(colon == string::npos) throw runtime_error("TorrentFile: invalid string structure in info dict");
                long long len = stoll(raw.substr(i, colon - i));
                i = colon + 1 + len;
            } else if(ch == 'i'){
                size_t end_e = raw.find('e', i);
                if (end_e == string::npos) throw runtime_error("TorrentFile: invalid integer structure in info dict");
                i = end_e + 1;
            } else {
                throw runtime_error("TorrentFile: unexpected structural character during info scanning");
            }
        }
        throw runtime_error("TorrentFile: info dict structure ended prematurely");
    }

    TorrentFile TorrentFile::from_file(const string& path){ //its static-ally declared
        string raw = read_file(path);

        BencodeValue root_val = bencode::decode(raw);
        Dict& root = get<Dict>(root_val.data);

        string info_raw = extract_info_raw(raw);
        string info_hash = sha1_raw(info_raw);

        if (!root.count("announce"))
            throw runtime_error("TorrentFile: missing 'announce' key");
        string announce = get<String>(root.at("announce").data);

        if (!root.count("info"))
            throw runtime_error("TorrentFile: missing 'info' key");
        Dict& info = get<Dict>(root.at("info").data);

        if (!info.count("name"))
            throw runtime_error("TorrentFile: missing 'name' in info");
        string name = get<String>(info.at("name").data);

        if (!info.count("piece length"))
            throw runtime_error("TorrentFile: missing 'piece length' in info");
        long long piece_length = get<Int>(info.at("piece length").data);

        long long total_length = 0;
        if (info.count("length")) {
            total_length = get<Int>(info.at("length").data);
        } 
        else if(info.count("files")){
            List& files = get<List>(info.at("files").data);
            for (auto& file_entry : files){
                Dict& fd = get<Dict>(file_entry.data);
                total_length += get<Int>(fd.at("length").data);
            }
        }
        else{
            throw runtime_error("TorrentFile: info dict has neither 'length' nor 'files'");
        }

        if(!info.count("pieces"))
            throw runtime_error("TorrentFile: missing 'pieces' in info");
        string pieces_raw = get<String>(info.at("pieces").data);

        if(pieces_raw.size()%20 != 0)
            throw runtime_error("TorrentFile: 'pieces' length is not a multiple of 20");

        vector<string> pieces;
        pieces.reserve(pieces_raw.size() / 20);
        for (size_t i = 0; i < pieces_raw.size(); i += 20)
            pieces.push_back(pieces_raw.substr(i, 20));

        TorrentFile tf;
        //move does 0-memory swap type shit
        tf.announce     = move(announce);
        if (root.count("announce-list")) {
            List& tiers = get<List>(root.at("announce-list").data);
            for (auto& tier : tiers) {
                // each tier is a list of URLs
                if (!holds_alternative<List>(tier.data)) continue;
                List& urls = get<List>(tier.data);
                for (auto& url_val : urls) {
                    if (!holds_alternative<String>(url_val.data)) continue;
                    string url = get<String>(url_val.data);
                    // avoid duplicating the primary announce
                    if (url != tf.announce)
                        tf.announce_list.push_back(url);
                }
            }
        }

        tf.name         = move(name);
        tf.length       = total_length;
        tf.piece_length = piece_length;
        tf.pieces       = move(pieces);
        tf.info_hash    = move(info_hash);

        return tf;
    }
}