#pragma once
#include <string>
#include "torrent/torrent_file.hpp"
#include "engine/piece_manager.hpp"

namespace bencode {
class PeerConnection {
    private:
        static bool read_exact(int sock_fd, unsigned char* buffer, int target_bytes);
    public:
        static int establish_handshake(const std::string& peer_ip, int peer_port, 
        const std::string& info_hash, const std::string& client_id);
        static void handle_message_stream(int sock_fd, const TorrentFile& tf, PieceManager& pm);
};
}