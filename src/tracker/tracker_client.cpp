#include "tracker/tracker_client.hpp"
#include "torrent/torrent_file.hpp"
#include "bencode/parser.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <cstring> // For std::memcpy

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h> // For signal handling
#include <openssl/ssl.h>
#include <openssl/err.h>

namespace bencode {

// Explicitly percent-encode raw binary data safely as a class member method
std::string TrackerClient::url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (unsigned char c : value) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << int(c) << std::nouppercase;
        }
    }
    return escaped.str();
}

std::string TrackerClient::generate_peer_id() {
    return "-CS0001-395479204264";
}

TrackerResponse TrackerClient::contact_tracker(const TorrentFile& tf, const std::string& peer_id) {
    // Tell Linux to ignore SIGPIPE so unexpected disconnects throw clean exceptions
    #ifndef MSG_NOSIGNAL
    signal(SIGPIPE, SIG_IGN);
    #endif

    std::string host = "torrent.ubuntu.com";
    std::string path = "/announce";
    int port = 443;
    
    std::string url = tf.announce;
    size_t proto_pos = url.find("://");
    if (proto_pos != std::string::npos) {
        std::string no_proto = url.substr(proto_pos + 3);
        size_t slash = no_proto.find('/');
        if (slash != std::string::npos) {
            host = no_proto.substr(0, slash);
            path = no_proto.substr(slash);
        } else {
            host = no_proto;
            path = "/";
        }
    }

    std::string encoded_hash = url_encode(tf.info_hash);
    std::string encoded_peer = url_encode(peer_id);

    std::cout << "Building HTTP secure payload request structure...\n";
    std::ostringstream request_stream;
    request_stream << "GET " << path 
                   << "?info_hash=" << encoded_hash 
                   << "&peer_id=" << encoded_peer 
                   << "&port=6881&uploaded=0&downloaded=0&left=0" 
                   << "&compact=1&event=started HTTP/1.1\r\n"
                   << "Host: " << host << "\r\n"
                   << "Connection: close\r\n"
                   << "Accept-Encoding: identity\r\n\r\n";
    
    std::string request = request_stream.str();

    // ---------------- OPENSSL SECURE SOCKET SETUP ----------------
    SSL_library_init();
    const SSL_METHOD* method = TLS_client_method();
    SSL_CTX* ctx = SSL_CTX_new(method);
    if (!ctx) throw std::runtime_error("Failed to create OpenSSL context structure");

    struct hostent* he = gethostbyname(host.c_str());
    if (!he || !he->h_addr_list[0]) {
        SSL_CTX_free(ctx);
        throw std::runtime_error("Failed to resolve tracker hostname via DNS");
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        SSL_CTX_free(ctx);
        throw std::runtime_error("Failed to allocate network socket descriptor");
    }

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    // Safely copy the 4-byte network address to avoid alignment crashes
    std::memcpy(&addr.sin_addr.s_addr, he->h_addr_list[0], he->h_length);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        close(sock);
        SSL_CTX_free(ctx);
        throw std::runtime_error("TCP handshake connection to remote tracker host failed");
    }

    SSL* ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sock);
    SSL_set_tlsext_host_name(ssl, host.c_str()); 

    if (SSL_connect(ssl) <= 0) {
        SSL_free(ssl);
        close(sock);
        SSL_CTX_free(ctx);
        throw std::runtime_error("SSL context handshake negotiation dropped by tracker");
    }

    // Send HTTP Request safely
    SSL_write(ssl, request.c_str(), request.length());

    // Read Response Buffer completely
    std::string response;
    char buffer[4096];
    int bytes_read;
    while ((bytes_read = SSL_read(ssl, buffer, sizeof(buffer))) > 0) {
        response.append(buffer, bytes_read);
    }

    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(sock);
    SSL_CTX_free(ctx);

    // Strip the HTTP text headers entirely before passing to your parser
    size_t header_end = response.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        throw std::runtime_error("Tracker wire back-channel returned an invalid HTTP block layout");
    }
    
    std::string bencode_body = response.substr(header_end + 4);

    if (response.find("200 OK") == std::string::npos) {
        throw std::runtime_error("Tracker returned HTTP error:\n" + response.substr(0, header_end));
    }

    // Decode the payload into your variant-backed BencodeValue AST
    auto decoded_ast = bencode::decode(bencode_body);
    TrackerResponse res;

    try {
        // Ensure the root layer of the tracker response is a Dictionary (Index 3)
        if (decoded_ast.data.index() != 3) {
            throw std::runtime_error("Tracker response root is not a bencode dictionary");
        }
        
        const auto& main_dict = std::get<Dict>(decoded_ast.data);

        // 1. Extract and process the compact 6-byte binary peers string (Index 1)
        auto peers_it = main_dict.find("peers");
        if (peers_it != main_dict.end() && peers_it->second.data.index() == 1) {
            std::string peers_blob = std::get<String>(peers_it->second.data);

            for (size_t i = 0; i + 6 <= peers_blob.length(); i += 6) {
                Peer p;
                
                // Read individual network address bytes
                unsigned char ip1 = peers_blob[i];
                unsigned char ip2 = peers_blob[i+1];
                unsigned char ip3 = peers_blob[i+2];
                unsigned char ip4 = peers_blob[i+3];
                
                p.ip = std::to_string(ip1) + "." + std::to_string(ip2) + "." + 
                       std::to_string(ip3) + "." + std::to_string(ip4);
                
                // Convert 2-byte network big-endian short to host byte order integer
                unsigned char p1 = peers_blob[i+4];
                unsigned char p2 = peers_blob[i+5];
                p.port = (p1 << 8) | p2;
                
                res.peers.push_back(p);
            }
        }

        // 2. Extract standard interval timing (Index 0)
        auto interval_it = main_dict.find("interval");
        if (interval_it != main_dict.end() && interval_it->second.data.index() == 0) {
            res.interval = std::get<Int>(interval_it->second.data);
        } else {
            res.interval = 1800; // 30-minute fallback default
        }

        // 3. Extract seeder statistics metrics safely
        auto complete_it = main_dict.find("complete");
        if (complete_it != main_dict.end() && complete_it->second.data.index() == 0) {
            res.seeders = std::get<Int>(complete_it->second.data);
        } else {
            res.seeders = 10;
        }

        auto incomplete_it = main_dict.find("incomplete");
        if (incomplete_it != main_dict.end() && incomplete_it->second.data.index() == 0) {
            res.leechers = std::get<Int>(incomplete_it->second.data);
        } else {
            res.leechers = 5;
        }

    } catch (const std::exception& e) {
        throw std::runtime_error("Parsing error extracting variant types from AST tree: " + std::string(e.what()));
    }

    return res;
}

} // namespace bencode