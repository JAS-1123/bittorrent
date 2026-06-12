#pragma once
#include <string>

namespace bencode {

    class PeerConnection {
    public:
        //connects to a target peer, exchanges the 68-byte handshake packet, and verifies authenticity.
        static bool establish_handshake(const std::string& peer_ip, int peer_port, 
                                        const std::string& info_hash, const std::string& client_id);
    };

}