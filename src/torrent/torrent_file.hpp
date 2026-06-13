#pragma once
#include "bencode/parser.hpp"
#include <string>
#include <vector>
#include <cstdint>

//torrent_file holds all metadata extracted from a .torrent file.
namespace bencode {
    struct TorrentFile {
        std::string announce;
        std::string name; //output file/folder name
        long long length; //total bytes (single-file torrent)
        long long piece_length; // bytes per piece
        std::vector<std::string> pieces; // raw SHA-1 hashes, 20 bytes each
        std::string info_hash; // raw 20-byte SHA-1 of bencoded info dict

        long long num_pieces() const {
            return (long long)pieces.size();
        }
        long long piece_offset(int idx) const {
            return (long long)idx * piece_length;
        }
        long long piece_size(int idx) const {
            long long offset = piece_offset(idx);
            long long remaining = length - offset;
            return remaining < piece_length ? remaining : piece_length;
        }
        static TorrentFile from_file(const std::string& path);
        std::vector<std::string> announce_list;
    };
}