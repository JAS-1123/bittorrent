#pragma once
#include "torrent/torrent_file.hpp"
#include <string>
#include <vector>

namespace bencode {
    //single downloader/uploader
    struct Peer {
        std::string ip;
        int port;
    };

    //unpacks the data sent back by the tracker server
    struct TrackerResponse{
        int interval;//how many seconds we should wait before asking the tracker again
        int seeders; //number of peers with 100% of the file
        int leechers; //number of peers currently downloading
        std::vector<Peer> peers; //actual collection of peer network addresses
    };

    class TrackerClient{
    public:
        //generates a random 20-byte peer_id unique to this runtime instance of your app
        static std::string generate_peer_id();
        static std::string url_encode(const std::string& raw_bytes);
        static TrackerResponse contact_tracker(const TorrentFile& tf, const std::string& peer_id);
    };

}