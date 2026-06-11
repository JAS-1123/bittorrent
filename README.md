# Asynchronous Multi-Threaded C++ BitTorrent Client

A high-performance, production-grade BitTorrent client written from scratch in modern C++ (C++20). This engine leverages a multi-threaded asynchronous architecture to manage dozens of peer connections simultaneously on a lock-free, non-blocking I/O event loop.

Designed with core systems programming principles in mind, this project demonstrates custom binary protocol parsing, thread-safe memory management, network optimization, and decoupled architecture—making it an ideal piece for deep systems engineering technical portfolios.

---

## 🏗️ System Architecture

The client utilizes a hybrid **Proactor Asynchronous I/O pattern** combined with dedicated component isolation to achieve high throughput without risking lock contention or CPU thread thrashing.

+-------------------------------------------------------+
|                     .torrent File                     |
+-------------------------------------------------------+
|
v [Parsed via Bencode]
+-------------------------------------------------------+
|                  Core Engine State                    |
|  - Global Pieces Scoreboard (Protected by std::mutex)  |
+-------------------------------------------------------+
|                                         |
v [Discovers Peer IPs]                    v [Pushes Blocks]
+-----------------------+              +-----------------------+
|    Tracker Client     |              |   DiskWriter Thread   |
|   (HTTP/UDP Sockets)  |              |  (Sequential pwrite)  |
+-----------------------+              +-----------------------+
^
| [Task Queue]
+-------------------------------------------------------+
|          Asynchronous Network Thread Pool             |
|     (Hardware Concurrency - io_context.run())         |
+-------------------------------------------------------+
|                        |                        |
v [Strand Isolated]      v [Strand Isolated]      v [Strand Isolated]
+--------------+         +--------------+         +--------------+
| Peer Socket  |         | Peer Socket  |         | Peer Socket  |
|  (Connection)|         |  (Connection)|         |  (Connection)|
+--------------+         +--------------+         +--------------+



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

[ 1. Initialization ] ────► Parse .torrent metadata & calculate 20-byte InfoHash
|
v
[ 2. Discovery ]      ────► Announce to Tracker ──► Fetch packed Peer IP:Port list
|
v
[ 3. Concurrency ]    ────► Boot Thread Pool (io_context) & spawn DiskWriter loop
|
v
[ 4. Wire Protocol ]  ────► Connect to Peers ──► Async Handshake ──► Share Bitfield
|
v
[ 5. Pipelining ]     ────► Loop: Request 16KB Blocks ──► Receive raw TCP streams
|
v
[ 6. Verification ]   ────► Run SHA-1 cryptographic integrity checks on pieces
|
┌────────────────┴────────────────┐
▼ [Hash Valid]                    ▼ [Hash Invalid]
Queue task for DiskWriter              Discard corrupted buffer
│                                 │
▼                                 ▼
Execute pwrite() to disk            Re-queue block requests


## ✨ Features

* 

---

## 🎥 Demo

* 

---

## 🚀 How To Use

*