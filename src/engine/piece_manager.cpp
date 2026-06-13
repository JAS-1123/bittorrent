#include "piece_manager.hpp"
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <openssl/sha.h>

using namespace std;

namespace bencode {

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────
PieceManager::PieceManager(const TorrentFile& tf, const string& output_path)
    : m_tf(tf), m_output_path(output_path)
{
    int num_pieces = (int)tf.pieces.size();

    // Allocate state for every piece
    m_pieces.resize(num_pieces);
    for (int i = 0; i < num_pieces; ++i) {
        int piece_len  = (int)tf.piece_size(i);
        int num_blocks = (piece_len + BLOCK_SIZE - 1) / BLOCK_SIZE;

        m_pieces[i].buffer.assign(piece_len, 0);
        m_pieces[i].blocks.resize(num_blocks);
        m_pieces[i].completed = false;
    }

    // Pre-allocate the output file to the full torrent size so we can
    // seek-write pieces out of order without gaps
    m_file.open(output_path, ios::in | ios::out | ios::binary | ios::trunc);
    if (!m_file.is_open()) {
        throw runtime_error("PieceManager: Cannot open output file: " + output_path);
    }

    // Expand file to full size by seeking and writing a null byte at the end
    m_file.seekp(tf.length - 1);
    m_file.write("\0", 1);
    if (!m_file.good()) {
        throw runtime_error("PieceManager: Failed to pre-allocate output file to " 
                            + to_string(tf.length) + " bytes");
    }

    cout << "📁 PieceManager: Output file ready → " << output_path << "\n";
    cout << "   Total pieces:  " << num_pieces << "\n";
    cout << "   Total size:    " << tf.length << " bytes\n\n";
}

PieceManager::~PieceManager() {
    if (m_file.is_open()) {
        m_file.flush();
        m_file.close();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Piece selection
// ─────────────────────────────────────────────────────────────────────────────
int PieceManager::pick_piece(const vector<bool>& peer_bitfield) {
    lock_guard<mutex> lock(m_mutex);

    for (int i = 0; i < (int)m_pieces.size(); ++i) {
        // Skip pieces we already finished
        if (m_pieces[i].completed) continue;

        // Skip pieces the peer doesn't have
        if (i >= (int)peer_bitfield.size() || !peer_bitfield[i]) continue;

        // Skip pieces where every block is already requested (mid-download)
        bool all_requested = true;
        for (const auto& b : m_pieces[i].blocks) {
            if (!b.requested) { all_requested = false; break; }
        }
        if (all_requested) continue;

        return i;
    }
    return -1; // nothing available from this peer right now
}

// ─────────────────────────────────────────────────────────────────────────────
//  Block lifecycle
// ─────────────────────────────────────────────────────────────────────────────
void PieceManager::mark_requested(int piece_index, int block_index) {
    lock_guard<mutex> lock(m_mutex);
    m_pieces[piece_index].blocks[block_index].requested = true;
}

bool PieceManager::store_block(int piece_index, int block_offset,
                                const unsigned char* data, int data_len) {
    lock_guard<mutex> lock(m_mutex);

    PieceState& ps = m_pieces[piece_index];

    if (ps.completed) return false; // already done, ignore late duplicate

    // Bounds check before memcpy
    if (block_offset + data_len > (int)ps.buffer.size()) {
        cerr << "PieceManager: Block overflows piece buffer for piece "
             << piece_index << " — dropping.\n";
        return false;
    }

    memcpy(ps.buffer.data() + block_offset, data, data_len);

    // Mark the matching block slot as received
    int block_index = block_offset / BLOCK_SIZE;
    if (block_index < (int)ps.blocks.size()) {
        ps.blocks[block_index].received = true;
    }

    // Check if every block in this piece has now arrived
    bool all_received = true;
    for (const auto& b : ps.blocks) {
        if (!b.received) { all_received = false; break; }
    }

    if (all_received) {
        return verify_and_flush(piece_index);
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
//  SHA-1 verify + flush to disk
// ─────────────────────────────────────────────────────────────────────────────
bool PieceManager::verify_and_flush(int piece_index) {
    PieceState& ps = m_pieces[piece_index];

    // SHA-1 the assembled buffer
    unsigned char calculated_hash[SHA_DIGEST_LENGTH];
    SHA1(ps.buffer.data(), ps.buffer.size(), calculated_hash);

    // Compare against the expected hash stored in TorrentFile::pieces
    const string& expected_hash = m_tf.pieces[piece_index];
    if (memcmp(calculated_hash, expected_hash.data(), 20) != 0) {
        cerr << "   ❌ SHA-1 FAILED for piece " << piece_index
             << " — discarding and re-queuing.\n";

        // Reset all block states so this piece gets re-requested
        for (auto& b : ps.blocks) {
            b.requested = false;
            b.received  = false;
        }
        ps.buffer.assign(ps.buffer.size(), 0);
        return false;
    }

    // Write verified bytes to the correct offset in the output file
    long long byte_offset = m_tf.piece_offset(piece_index);
    m_file.seekp(byte_offset);
    m_file.write(reinterpret_cast<char*>(ps.buffer.data()), (streamsize)ps.buffer.size());

    if (!m_file.good()) {
        cerr << "   ❌ Disk write failed for piece " << piece_index << ".\n";
        return false;
    }
    m_file.flush();

    ps.completed = true;
    m_pieces_done++;

    int total = (int)m_pieces.size();
    int pct   = (m_pieces_done * 100) / total;
    cout << "   ✅ Piece " << piece_index << " verified and saved. "
         << "Progress: " << m_pieces_done << "/" << total
         << " (" << pct << "%)\n";

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Status queries
// ─────────────────────────────────────────────────────────────────────────────
bool PieceManager::piece_completed(int piece_index) const {
    lock_guard<mutex> lock(m_mutex);
    return m_pieces[piece_index].completed;
}

bool PieceManager::all_done() const {
    lock_guard<mutex> lock(m_mutex);
    return m_pieces_done == (int)m_pieces.size();
}

int PieceManager::pieces_done() const {
    lock_guard<mutex> lock(m_mutex);
    return m_pieces_done;
}

int PieceManager::blocks_in_piece(int piece_index) const {
    int piece_len = (int)m_tf.piece_size(piece_index);
    return (piece_len + BLOCK_SIZE - 1) / BLOCK_SIZE;
}

int PieceManager::block_length(int piece_index, int block_index) const {
    int piece_len    = (int)m_tf.piece_size(piece_index);
    int block_start  = block_index * BLOCK_SIZE;
    int remaining    = piece_len - block_start;
    return remaining < BLOCK_SIZE ? remaining : BLOCK_SIZE;
}

} // namespace bencode