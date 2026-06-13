#pragma once
#include <string>
#include "torrent/torrent_file.hpp"

namespace bencode {
class PeerConnection {
private:
        // Helper function to reliably read an exact number of bytes from the TCP stream
static bool read_exact(int sock_fd, unsigned char* buffer, int target_bytes);
public:
        // Connects to a target peer and handshakes. Returns the active socket file descriptor (or -1 on failure).
static int establish_handshake(const std::string& peer_ip, int peer_port, 
const std::string& info_hash, const std::string& client_id);
        // Runs the continuous stream loop to read, process, and act on post-handshake protocol messages
static void handle_message_stream(int sock_fd, const TorrentFile& tf);
    };
} // namespace bencode