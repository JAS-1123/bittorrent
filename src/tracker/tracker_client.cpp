#include "tracker_client.hpp"
#include "bencode/parser.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <random>
#include <stdexcept>
#include <cstring>

//socket headers
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>

//openSSL headers for HTTPS encryption
#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

using namespace std;

namespace bencode {

    static void parse_url(const string& url, string& host, string& port, string& path, bool& is_https){
        string http_prefix = "http://";
        string https_prefix = "https://";

        int start = 0;
        if(url.substr(0, (int)https_prefix.size()) == https_prefix){
            is_https = true;
            start = (int)https_prefix.size();
            port = "443"; //standard HTTPS port
        }
        else if(url.substr(0, (int)http_prefix.size()) == http_prefix){
            is_https = false;
            start = (int)http_prefix.size();
            port = "80";  //standard HTTP port
        }
        else{
            throw runtime_error("TrackerClient: Unsupported protocol scheme (must be http or https)");
        }

        int slash = (int)url.find('/', start);
        string authority = (slash == -1) ? url.substr(start) : url.substr(start, slash - start);
        path = (slash == -1) ? "/" : url.substr(slash);

        int colon = (int)authority.find(':');
        if(colon != -1){
            host = authority.substr(0, colon);
            port = authority.substr(colon + 1);
        } 
        else{
            host = authority;
        }
    }

    string TrackerClient::generate_peer_id(){
        string id = "-CS0001-"; 
        random_device rd;
        mt19937 gen(rd());
        uniform_int_distribution<> dis(0, 9);
        
        while((int)id.size() < 20){
            id += to_string(dis(gen));
        }
        return id;
    }

    string TrackerClient::url_encode(const string& raw_bytes){
        ostringstream escaped; //creates a output stream like cout but in a buffer memory
        escaped << hex << uppercase;
        
        for(unsigned char c : raw_bytes){
            if(isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                escaped << c;
            } 
            else{
                escaped << '%' << setw(2) << setfill('0') << (int)c;
            }
        }
        return escaped.str();
    }

    TrackerResponse TrackerClient::contact_tracker(const TorrentFile& tf, const string& peer_id) {
        string host, port, path;
        bool is_https = false;
        parse_url(tf.announce, host, port, path, is_https);

        ostringstream url_builder;
        url_builder << path 
                    << "?info_hash=" << url_encode(tf.info_hash)
                    << "&peer_id=" << url_encode(peer_id)
                    << "&port=6881"   
                    << "&uploaded=0"
                    << "&downloaded=0"
                    << "&left=" << tf.length
                    << "&compact=1";  

        string full_request_path = url_builder.str();

        struct addrinfo hints, *res;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET; 
        hints.ai_socktype = SOCK_STREAM;

        if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0){
            throw runtime_error("TrackerClient: Failed to resolve DNS hostname: " + host);
        }

        int sock_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if(sock_fd < 0){
            freeaddrinfo(res);
            throw runtime_error("TrackerClient: Failed to construct socket");
        }

        if(connect(sock_fd, res->ai_addr, res->ai_addrlen) < 0){
            close(sock_fd);
            freeaddrinfo(res);
            throw runtime_error("TrackerClient: Connection timeout to: " + host);
        }
        freeaddrinfo(res); 

        ostringstream http_packet;
        http_packet << "GET " << full_request_path << " HTTP/1.1\r\n"
                    << "Host: " << host << "\r\n"
                    << "User-Agent: BitTorrentClient/1.0\r\n"
                    << "Connection: close\r\n\r\n";
        string request_raw = http_packet.str();

        string response_raw;
        char buffer[4096];

        if (is_https){
            OpenSSL_add_all_algorithms();
            SSL_load_error_strings();
            const SSL_METHOD* method = TLS_client_method();
            SSL_CTX* ctx = SSL_CTX_new(method);
            if(!ctx){
                close(sock_fd);
                throw runtime_error("TrackerClient: Failed to initialize SSL context");
            }

            SSL* ssl = SSL_new(ctx);
            SSL_set_fd(ssl, sock_fd);

            SSL_set_tlsext_host_name(ssl, host.c_str());

            if (SSL_connect(ssl) <= 0){
                SSL_free(ssl);
                SSL_CTX_free(ctx);
                close(sock_fd);
                throw runtime_error("TrackerClient: SSL secure handshake failed");
            }

            if(SSL_write(ssl, request_raw.c_str(), (int)request_raw.size()) <= 0){
                SSL_free(ssl);
                SSL_CTX_free(ctx);
                close(sock_fd);
                throw runtime_error("TrackerClient: SSL transmission failure");
            }

            int bytes_read;
            while((bytes_read = SSL_read(ssl, buffer, sizeof(buffer))) > 0){
                response_raw.append(buffer, bytes_read);
            }

            SSL_shutdown(ssl);
            SSL_free(ssl);
            SSL_CTX_free(ctx);
        } 
        else{
            if(send(sock_fd, request_raw.c_str(), (int)request_raw.size(), 0) < 0){
                close(sock_fd);
                throw runtime_error("TrackerClient: Plaintext transmission failure");
            }

            ssize_t bytes_read;
            while ((bytes_read = recv(sock_fd, buffer, sizeof(buffer), 0)) > 0) {
                response_raw.append(buffer, bytes_read);
            }
        }
        close(sock_fd);

        int header_end_pos = (int)response_raw.find("\r\n\r\n");
        if(header_end_pos == -1){
            throw runtime_error("TrackerClient: Received malformed HTTP response headers");
        }
        string body = response_raw.substr(header_end_pos + 4);

        if(body.empty()){
            throw runtime_error("TrackerClient: Server returned an empty body payload");
        }

        BencodeValue decoded_root = bencode::decode(body);
        Dict& root = get<Dict>(decoded_root.data);

        if (root.count("failure reason")) {
            throw runtime_error("TrackerClient dropped failure message: " + get<String>(root.at("failure reason").data));
        }

        TrackerResponse tr;
        tr.interval = (int)get<Int>(root.at("interval").data);
        tr.seeders  = root.count("complete") ? (int)get<Int>(root.at("complete").data) : 0;
        tr.leechers = root.count("incomplete") ? (int)get<Int>(root.at("incomplete").data) : 0;

        if(!root.count("peers")){
            throw runtime_error("TrackerClient: Missing 'peers' block");
        }

        if (holds_alternative<String>(root.at("peers").data)){
            string compact_peers = get<String>(root.at("peers").data);
            if(compact_peers.size() % 6 != 0){
                throw runtime_error("TrackerClient: Compact peer string length is not a multiple of 6");
            }

            for(int i = 0; i < (int)compact_peers.size(); i += 6){
                Peer p;
                unsigned char ip1 = compact_peers[i];
                unsigned char ip2 = compact_peers[i + 1];
                unsigned char ip3 = compact_peers[i + 2];
                unsigned char ip4 = compact_peers[i + 3];
                p.ip = to_string(ip1) + "." + to_string(ip2) + "." + to_string(ip3) + "." + to_string(ip4);

                unsigned char port_high = compact_peers[i + 4];
                unsigned char port_low  = compact_peers[i + 5];
                p.port = (port_high << 8) | port_low;

                tr.peers.push_back(move(p));
            }
        } 
        else if (holds_alternative<List>(root.at("peers").data)){
            List& peer_list = get<List>(root.at("peers").data);
            for (const auto& entry : peer_list) {
                const Dict& peer_dict = get<Dict>(entry.data);
                Peer p;
                p.ip   = get<String>(peer_dict.at("ip").data);
                p.port = (int)get<Int>(peer_dict.at("port").data);
                tr.peers.push_back(move(p));
            }
        }

        return tr;
    }
}