#include "peer_connection.hpp"
#include "engine/piece_manager.hpp"
#include "message.hpp" 
#include <iostream>
#include <cstring>
#include <vector>
#include <stdexcept>
#include <algorithm>

#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <openssl/sha.h>

using namespace std;

namespace bencode {

    bool PeerConnection::read_exact(int sock_fd, unsigned char* buffer, int target_bytes){
        int total_received = 0;
        while (total_received < target_bytes){
            int bytes_read = recv(sock_fd, buffer + total_received, target_bytes - total_received, 0);
            if(bytes_read < 0){
                if(errno == EAGAIN || errno == EWOULDBLOCK) {
                    cerr << "Socket Read Operation timed out after waiting on wire.\n";
                }
                return false;
            }
            if(bytes_read == 0){
                return false;
            }
            total_received += bytes_read;
        }
        return true;
    }

    int PeerConnection::establish_handshake(const string& peer_host, int peer_port, 
                                            const string& info_hash, const string& client_id){
        char handshake_out[68];
        memset(handshake_out, 0, 68);

        handshake_out[0] = 19; 
        memcpy(&handshake_out[1], "BitTorrent protocol", 19);
        memcpy(&handshake_out[28], info_hash.c_str(), 20);
        memcpy(&handshake_out[48], client_id.c_str(), 20);

        struct addrinfo hints, *server_info;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET; 
        hints.ai_socktype = SOCK_STREAM;

        string port_str = to_string(peer_port);
        if (getaddrinfo(peer_host.c_str(), port_str.c_str(), &hints, &server_info) != 0){
            cerr << "PeerConnection: Failed to resolve peer address target: " << peer_host << "\n";
            return -1;
        }

        int sock_fd = socket(server_info->ai_family, server_info->ai_socktype, server_info->ai_protocol);
        if (sock_fd < 0) {
            cerr << "PeerConnection: Failed to allocate hardware socket descriptor.\n";
            freeaddrinfo(server_info);
            return -1;
        }

        struct timeval timeout;
        timeout.tv_sec = 10; 
        timeout.tv_usec = 0;
        setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
        setsockopt(sock_fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

        cout << "Connecting directly to remote swarm machine [" << peer_host << ":" << peer_port << "]...\n";
        if (connect(sock_fd, server_info->ai_addr, server_info->ai_addrlen) < 0) {
            cerr << "PeerConnection: Direct socket connection failed or timed out.\n";
            close(sock_fd);
            freeaddrinfo(server_info);
            return -1;
        }
        freeaddrinfo(server_info);

        cout << "Connection active! Transmitting 68-byte cryptographic handshake passport...\n";
        if (send(sock_fd, handshake_out, 68, 0) < 0){
            cerr << "PeerConnection: Write block transmission failure.\n";
            close(sock_fd);
            return -1;
        }

        char handshake_in[68];
        memset(handshake_in, 0, 68);

        if(!read_exact(sock_fd, (unsigned char*)handshake_in, 68)){
            cerr << "PeerConnection: Remote machine disconnected during handshake response loop.\n";
            close(sock_fd);
            return -1;
        }

        if(handshake_in[0] != 19 || memcmp(&handshake_in[1], "BitTorrent protocol", 19) != 0){
            cerr << "PeerConnection: Protocol validation anomaly.\n";
            close(sock_fd);
            return -1;
        }

        string returned_hash(&handshake_in[28], 20);
        if(returned_hash != info_hash){
            cerr << "PeerConnection: Swarm crossover mismatch anomaly. Peer has a different file!\n";
            close(sock_fd);
            return -1;
        }

        cout << "✓ Success! Handshake fully authenticated with remote node.\n";
        return sock_fd;
    }

void PeerConnection::handle_message_stream(int sock_fd, const TorrentFile& tf, PieceManager& pm) {        vector<unsigned char> interested_packet = Message::create_interested();
        if (send(sock_fd, interested_packet.data(), (int)interested_packet.size(), 0) < 0) {
            cerr << "PeerConnection: Failed to transmit initial Interested packet.\n";
            close(sock_fd);
            return;
        }
        cout << "Sent [Interested] state packet to remote machine. Awaiting unchoke response...\n";
        cout << "Entering Peer Wire Message Stream Loop...\n\n";

        bool am_unchoked = false;
        
        int target_piece_index = -1;
        int block_size = 16384; // standard 16KB torrent block size
        
        int total_blocks_in_piece = 0;
        int blocks_requested = 0;
        int blocks_received = 0;
        vector<unsigned char> piece_buffer;
        vector<bool> peer_bitfield;

        while(true){
            
            unsigned char length_buffer[4];            
            if(!read_exact(sock_fd, length_buffer, 4)){
                cout << "Peer disconnected or stream connection lost.\n";
                break;
            }

            int msg_length = (length_buffer[0] << 24) | 
                             (length_buffer[1] << 16) | 
                             (length_buffer[2] << 8)  | 
                              length_buffer[3];

            if(msg_length == 0){
                cout << "[Message] Keep-Alive received.\n";
                continue;
            }

            unsigned char msg_id;
            if(!read_exact(sock_fd, &msg_id, 1)){
                break;
            }

            int payload_length = msg_length - 1;
            vector<unsigned char> payload(payload_length);
            
            if(payload_length > 0){
                if(!read_exact(sock_fd, payload.data(), payload_length)){
                    break;
                }
            }

            switch (static_cast<MessageId>(msg_id)) {
                case MessageId::CHOKE:
                    cout << "[Message] Choke: Remote peer choked communication lanes.\n";
                    am_unchoked = false;
                    break;
                    
                case MessageId::UNCHOKE:
                    cout << "[Message] Unchoke: Peer cleared you to download data!\n";
                    am_unchoked = true;
                    break;
                    
                case MessageId::HAVE: {
                    int piece_index = (payload[0] << 24) | (payload[1] << 16) | (payload[2] << 8) | payload[3];
                    cout << "[Message] Have: Peer has piece index [" << piece_index << "]\n";
                    
                    if (peer_bitfield.size() <= static_cast<size_t>(piece_index)){
                        peer_bitfield.resize(piece_index + 1, false);
                    }
                    peer_bitfield[piece_index] = true;

                    if (target_piece_index == -1){
                        target_piece_index = piece_index; 
                    }
                    break;
                }
                
                case MessageId::BITFIELD: {
                    cout << "[Message] Bitfield Payload Mapping Caught! Size: " << payload_length << " bytes.\n";
                    peer_bitfield.assign(payload_length * 8, false);
                    
                    int first_available_piece = -1;
                    for (int byte_idx = 0; byte_idx < payload_length; ++byte_idx) {
                        unsigned char current_byte = payload[byte_idx];
                        for (int bit_idx = 0; bit_idx < 8; ++bit_idx) {
                            bool has_piece = (current_byte >> (7 - bit_idx)) & 1;
                            int current_piece = (byte_idx * 8) + bit_idx;
                            peer_bitfield[current_piece] = has_piece;
                            
                            if (has_piece && first_available_piece == -1) {
                                first_available_piece = current_piece;
                            }
                        }
                    }
                    if (first_available_piece != -1 && target_piece_index == -1) {
                        target_piece_index = first_available_piece;
                        cout << "Target locked on available piece [" << target_piece_index << "]\n";
                    }
                    break;
                }

                case MessageId::PIECE: {
                    int piece_idx = (payload[0] << 24) | (payload[1] << 16) | (payload[2] << 8) | payload[3];
                    int block_off = (payload[4] << 24) | (payload[5] << 16) | (payload[6] << 8) | payload[7];
                    int data_size = payload_length - 8;

                    if (piece_idx == target_piece_index) {
                        cout << " Received block for Piece " << piece_idx
                            << " offset " << block_off << " (" << data_size << " bytes)\n";

                        bool piece_done = pm.store_block(piece_idx, block_off, &payload[8], data_size);
                        blocks_received++;

                        if (piece_done) {
                            peer_bitfield[piece_idx] = false;
                            target_piece_index    = pm.pick_piece(peer_bitfield);
                            blocks_requested      = 0;
                            blocks_received       = 0;
                            total_blocks_in_piece = 0;

                            if (pm.all_done()) {
                                cout << "All pieces downloaded. Stream complete.\n";
                                close(sock_fd);
                                return;
                            }
                            if (target_piece_index == -1) {
                                cout << "Peer has no more pieces we need right now. Waiting...\n";
                            } else {
                                cout << "Relocking onto next piece [" << target_piece_index << "]\n";
                            }
                        }
                    }
                    break;
                }
                default:
                    break;
            }

            if (am_unchoked && target_piece_index != -1) {
                if (total_blocks_in_piece == 0) {
                    int current_piece_size = (int)tf.piece_size(target_piece_index);

                    total_blocks_in_piece = (current_piece_size + block_size - 1) / block_size;
                    piece_buffer.assign(current_piece_size, 0);
                }

                while (blocks_requested < total_blocks_in_piece && (blocks_requested - blocks_received) < 4) {
                    int offset = blocks_requested * block_size;
                    int current_block_request_len = min(block_size, (int)piece_buffer.size() - offset);

                    vector<unsigned char> request_packet = Message::create_request(target_piece_index, offset, current_block_request_len);
                    
                    if (send(sock_fd, request_packet.data(), (int)request_packet.size(), 0) < 0) {
                        cerr << " Failed to transmit block request packet.\n";
                        break;
                    }
                    blocks_requested++;
                }
            }
        }
        close(sock_fd);
    }

}