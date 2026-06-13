#include "torrent/torrent_file.hpp"
#include "tracker/tracker_client.hpp"
#include "network/peer_connection.hpp"
#include "engine/piece_manager.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>

using namespace std;
using namespace bencode;

// Global print mutex so logs from different threads don't interleave
mutex g_print_mutex;

void log(const string& msg) {
    lock_guard<mutex> lock(g_print_mutex);
    cout << msg << "\n";
}

// Each thread runs this — connects to one peer and downloads as much as it can
void peer_worker(const Peer& peer, const TorrentFile& tf,
                 PieceManager& pm, const string& peer_id,
                 atomic<int>& active_threads) {

    string tag = "[" + peer.ip + ":" + to_string(peer.port) + "] ";

    int sock_fd = PeerConnection::establish_handshake(peer.ip, peer.port,
                                                      tf.info_hash, peer_id);
    if (sock_fd == -1) {
        log(tag + "✗ Handshake failed.");
        active_threads--;
        return;
    }

    log(tag + "✅ Handshake success.");
    PeerConnection::handle_message_stream(sock_fd, tf, pm);
    log(tag + "⬛ Session ended. Progress: "
        + to_string(pm.pieces_done()) + "/" + to_string(pm.total_pieces()));

    active_threads--;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "Usage: ./torrent_client <path_to_torrent_file>\n";
        return 1;
    }

    // ── Step 1: Parse .torrent ────────────────────────────────────────────────
    cout << "═══════════════════════════════════════════════\n";
    cout << "  Step 1: Parsing .torrent metadata file\n";
    cout << "═══════════════════════════════════════════════\n";

    TorrentFile tf;
    try {
        tf = TorrentFile::from_file(argv[1]);
    } catch (const exception& e) {
        cerr << "❌ Failed to parse torrent file: " << e.what() << "\n";
        return 1;
    }

    cout << "✅ Torrent parsed!\n";
    cout << "   Name:           " << tf.name << "\n";
    cout << "   Size:           " << tf.length << " bytes\n";
    cout << "   Pieces:         " << tf.pieces.size() << "\n";
    cout << "   Piece length:   " << tf.piece_length << " bytes\n";
    cout << "   Extra trackers: " << tf.announce_list.size() << "\n\n";

    // ── Step 2: Collect peers from all trackers ───────────────────────────────
    cout << "═══════════════════════════════════════════════\n";
    cout << "  Step 2: Contacting all trackers\n";
    cout << "═══════════════════════════════════════════════\n";

    string peer_id = TrackerClient::generate_peer_id();
    cout << "   peer_id: " << peer_id << "\n\n";

    vector<Peer> all_peers;
    vector<string> all_trackers;
    all_trackers.push_back(tf.announce);
    for (const auto& url : tf.announce_list)
        all_trackers.push_back(url);

    for (const auto& tracker_url : all_trackers) {
        cout << "→ Tracker: " << tracker_url << "\n";
        try {
            TorrentFile tf_copy = tf;
            tf_copy.announce = tracker_url;
            TrackerResponse tr = TrackerClient::contact_tracker(tf_copy, peer_id);
            cout << "  ✅ Got " << tr.peers.size() << " peers"
                 << " (S:" << tr.seeders << " L:" << tr.leechers << ")\n";
            for (auto& p : tr.peers)
                all_peers.push_back(p);
        } catch (const exception& e) {
            cout << "  ✗ " << e.what() << "\n";
        }
    }

    // Deduplicate
    sort(all_peers.begin(), all_peers.end(), [](const Peer& a, const Peer& b){
        return a.ip < b.ip || (a.ip == b.ip && a.port < b.port);
    });
    all_peers.erase(unique(all_peers.begin(), all_peers.end(), [](const Peer& a, const Peer& b){
        return a.ip == b.ip && a.port == b.port;
    }), all_peers.end());

    cout << "\n   Total unique peers: " << all_peers.size() << "\n\n";

    if (all_peers.empty()) {
        cerr << "❌ No peers found.\n";
        return 1;
    }

    // ── Step 3: PieceManager ──────────────────────────────────────────────────
    cout << "═══════════════════════════════════════════════\n";
    cout << "  Step 3: Initialising PieceManager\n";
    cout << "═══════════════════════════════════════════════\n";

    PieceManager pm(tf, tf.name);

    // ── Step 4: Multithreaded download ────────────────────────────────────────
    cout << "═══════════════════════════════════════════════\n";
    cout << "  Step 4: Launching peer threads\n";
    cout << "═══════════════════════════════════════════════\n\n";

    const int MAX_THREADS = 20; // max simultaneous peer connections
    vector<thread> threads;
    atomic<int> active_threads(0);

    for (const auto& peer : all_peers) {
        if (pm.all_done()) break;

        // Wait if we're at the thread cap
        while (active_threads >= MAX_THREADS) {
            this_thread::sleep_for(chrono::milliseconds(200));
        }

        active_threads++;
        threads.emplace_back(peer_worker, cref(peer), cref(tf),
                             ref(pm), cref(peer_id), ref(active_threads));
    }

    // Wait for all threads to finish
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    // ── Step 5: Result ────────────────────────────────────────────────────────
    cout << "\n═══════════════════════════════════════════════\n";
    if (pm.all_done()) {
        cout << "  ✅ Download complete! Saved to: " << tf.name << "\n";
    } else {
        cout << "  ⚠️  Incomplete: " << pm.pieces_done()
             << "/" << pm.total_pieces() << " pieces.\n";
        cout << "  Re-run to continue.\n";
    }
    cout << "═══════════════════════════════════════════════\n";

    return 0;
}