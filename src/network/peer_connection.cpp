#include "peer_connection.hpp"
#include <iostream>
#include <cstring>
#include <stdexcept>

//linux network socket headers
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace std;

namespace bencode{
    bool PeerConnection::establish_handshake(const string& peer_ip, int peer_port, 
                                            const string& info_hash, const string& client_id){
        
        //our raw 68-byte outgoing packet buffer
        char handshake_out[68];
        memset(handshake_out, 0, 68);

        //byte 0: string length of protocol id (19)
        //bytes 1-19: protocol name string
        //bytes 20-27: reserved extension flags (left as zeros)
        //bytes 28-47: the target 20-byte Info Hash
        //bytes 48-67: our 20-byte Client Session ID
        handshake_out[0] = 19; 
        memcpy(&handshake_out[1], "BitTorrent protocol", 19);
        memcpy(&handshake_out[28], info_hash.c_str(), 20);
        memcpy(&handshake_out[48], client_id.c_str(), 20);

        int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_fd < 0) {
            cerr << "PeerConnection: Failed to allocate hardware socket descriptor.\n";
            return false;
        }
        struct timeval timeout;
        timeout.tv_sec = 7; // 7 seconds limit
        timeout.tv_usec = 0;
        setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
        setsockopt(sock_fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
        
        struct sockaddr_in peer_addr;
        memset(&peer_addr, 0, sizeof(peer_addr));
        peer_addr.sin_family = AF_INET;
        peer_addr.sin_port = htons(peer_port);

        if (inet_pton(AF_INET, peer_ip.c_str(), &peer_addr.sin_addr) <= 0) {
            cerr << "PeerConnection: Invalid IP formatting structure: " << peer_ip << "\n";
            close(sock_fd);
            return false;
        }

        cout << "Connecting directly to remote swarm machine [" << peer_ip << ":" << peer_port << "]...\n";
        if(connect(sock_fd, (struct sockaddr*)&peer_addr, sizeof(peer_addr)) < 0){
            cerr << "PeerConnection: Direct socket connection failed or timed out.\n";
            close(sock_fd);
            return false;
        }

        cout << "Connection active! Transmitting 68-byte cryptographic handshake passport...\n";
        if(send(sock_fd, handshake_out, 68, 0) < 0){
            cerr << "PeerConnection: Write block transmission failure.\n";
            close(sock_fd);
            return false;
        }

        char handshake_in[68];
        memset(handshake_in, 0, 68);
        int total_received = 0;

        while(total_received < 68){
            int bytes_read = recv(sock_fd, &handshake_in[total_received], 68 - total_received, 0);
            if(bytes_read <= 0){
                cerr << "PeerConnection: Remote machine disconnected or timed out during handshake response loop.\n";
                close(sock_fd);
                return false;
            }
            total_received += bytes_read;
        }
        close(sock_fd);

        if (handshake_in[0] != 19 || memcmp(&handshake_in[1], "BitTorrent protocol", 19) != 0){
            cerr << "PeerConnection: Protocol validation anomaly. Client speaks alternative format language.\n";
            return false;
        }

        string returned_hash(&handshake_in[28], 20);
        if (returned_hash != info_hash) {
            cerr << "PeerConnection: Swarm crossover mismatch anomaly. Peer is transferring an entirely different file context!\n";
            return false;
        }
        cout << "✓ Success! Handshake fully authenticated with remote node.\n";
        return true;
    }

}