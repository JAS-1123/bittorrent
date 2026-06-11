# Asynchronous Multi-Threaded C++ BitTorrent Client

A high-performance, production-grade BitTorrent client written from scratch in modern C++ (C++20). This engine leverages a multi-threaded asynchronous architecture to manage dozens of peer connections simultaneously on a lock-free, non-blocking I/O event loop.

Designed with core systems programming principles in mind, this project demonstrates custom binary protocol parsing, thread-safe memory management, network optimization, and decoupled architecture—making it an ideal piece for deep systems engineering technical portfolios.

---

## 🏗️ System Architecture

The client utilizes a hybrid **Proactor Asynchronous I/O pattern** combined with dedicated component isolation to achieve high throughput without risking lock contention or CPU thread thrashing.

cliTorrent/
├── core/
│   ├── bencode.py          # Custom Bencode parser (decodes raw torrent files)
│   ├── torrent.py          # Metadata extractor (calculates info-hashes, parses file layouts)
│   └── tracker.py          # Tracker client (manages HTTP, HTTPS, and UDP protocol announcements)
├── network/
│   ├── connection.py       # Low-level TCP socket manager & handshake handler
│   ├── protocol.py         # BitTorrent wire protocol implementation (Choke, Unchoke, Interested, Piece)
│   └── peer_worker.py      # Asyncio worker pool managing concurrent peer download loops
└── storage/
    ├── manager.py          # Block assembler & multi-file/single-file disk writer
    └── verifier.py         # SHA-1 piece validator against torrent file metadata



### Component Breakdown & Concurrency Boundaries

* **Network Thread Pool (`boost::asio::io_context`)**
  Spawns a group of native threads matching `std::thread::hardware_concurrency()`. All threads call `io_context::run()` simultaneously, pulling network event completion notifications out of a shared kernel infrastructure (`epoll` on Linux, `IOCP` on Windows).
* **Peer Connections (`boost::asio::strand`)**
  Each peer connection represents an independent state machine (`am_choking`, `peer_interested`, etc.). To prevent data races without sacrificing multi-threaded performance, every connection runs within its own **Asio Strand**. The strand serializes network callback handler executions for that socket across the pool, making peer interactions effectively lock-free.
* **Global Scoreboard (`PieceManager`)**
  A synchronized central database containing the global bitfield of the torrent. When threads request new blocks for their designated peer, they safely pull them via a highly fast, critical section protected by an internal `std::mutex`.
* **Isolated Storage Pipeline (`DiskWriter`)**
  Network worker threads do not perform disk file operations. When a block passes its SHA-1 cryptographic validation, it is packaged as a task and pushed to a synchronized thread-safe queue. A single, dedicated background thread pops data sequentially, preventing disk head thrashing and file descriptor race conditions.

---

## 🔄 Core Application Flow

[Start Execution]
└── 1. Metadata Parsing Phase
    └── Read local .torrent binary file
        └── Parse file structure via custom Bencode decoder
            ├── Extract essential dictionary keys (announce, info, piece length)
            └── Compute 20-byte SHA-1 Info Hash from the raw 'info' block
└── 2. Peer Discovery Phase
    └── Build tracker request payload (Info Hash, Peer ID, Port, Stats)
        └── Announce to Trackers sequentially (HTTP/HTTPS or UDP)
            └── Receive compact/binary tracker response
                └── Decode 6-byte network strings into IPv4 addresses and port numbers
└── 3. Swarm Orchestration Phase (Asyncio Loop)
    └── Spawn concurrent peer connection workers
        └── Initialize TCP handshake with available peer addresses
            ├── Validate 68-byte BitTorrent protocol handshake reply
            ├── Exchange Bitfield payloads to build global piece-availability map
            └── Send 'Interested' signal & await peer 'Unchoke' state transition
└── 4. Pipelined Download Phase
    └── Target incomplete piece sequence
        └── Slice target pieces into standard 16 KB blocks
            ├── Dispatch concurrent 'Request' messages to unchoked peers
            ├── Monitor timeout thresholds -> Evict stalled/bad peers via Peer Rotation
            └── Stream raw block chunks from network buffer into active memory
└── 5. Integrity & Committal Phase
    └── Aggregate all 16 KB blocks belonging to the target piece
        └── Run SHA-1 hash function over the assembled byte array
            ├── Match? Yes ──> Pass to Storage Manager ──> Seek disk offset ──> Commit to disk
            └── Match? No  ──> Purge corrupted blocks ──> Return piece indices to download queue
└── [Download Complete]


## ✨ Features

* 

---

## 🎥 Demo

* 

---

## 🚀 How To Use

*