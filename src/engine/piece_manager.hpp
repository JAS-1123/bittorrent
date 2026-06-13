#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <fstream>
#include "../torrent/torrent_file.hpp"

namespace bencode {

// Tracks the state of every block inside a single piece
struct BlockState {
    bool requested = false;
    bool received  = false;
};

// Tracks the state of a single piece across all its blocks
struct PieceState {
    std::vector<BlockState>      blocks;       // one entry per 16KB block
    std::vector<unsigned char>   buffer;       // raw assembled bytes
    bool                         completed = false;
};

class PieceManager {
public:
    static constexpr int BLOCK_SIZE    = 16384; // 16 KiB standard block
    static constexpr int MAX_PIPELINE  = 5;     // max outstanding requests

    // Initialises state for every piece and opens the output file
    PieceManager(const TorrentFile& tf, const std::string& output_path);
    ~PieceManager();

    // ── Piece selection ───────────────────────────────────────────────────────

    // Returns the next piece index this manager wants downloaded,
    // filtered against the peer's bitfield. Returns -1 if nothing needed.
    int pick_piece(const std::vector<bool>& peer_bitfield);

    // ── Block lifecycle ───────────────────────────────────────────────────────

    // Mark a block as requested so we don't double-request it
    void mark_requested(int piece_index, int block_index);

    // Store a received block into the piece buffer.
    // Returns true if this block completes the piece (triggers SHA-1 + flush).
    bool store_block(int piece_index, int block_offset,
                     const unsigned char* data, int data_len);

    // ── Status queries ────────────────────────────────────────────────────────

    bool piece_completed(int piece_index) const;
    bool all_done()                        const;
    int  pieces_done()                     const;
    int  total_pieces()                    const { return (int)m_pieces.size(); }

    // How many blocks are in a given piece (accounts for last-piece truncation)
    int blocks_in_piece(int piece_index) const;

    // Byte length of one specific block (accounts for last-block truncation)
    int block_length(int piece_index, int block_index) const;

private:
    const TorrentFile&           m_tf;
    std::string                  m_output_path;
    std::vector<PieceState>      m_pieces;
    int                          m_pieces_done = 0;
    mutable std::mutex           m_mutex;       // guards all state + file writes
    std::fstream                 m_file;        // output file handle (random-access)

    // SHA-1 verify + flush piece buffer to disk. Called internally by store_block.
    bool verify_and_flush(int piece_index);
};

} // namespace bencode