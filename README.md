# Asynchronous Multi-Threaded C++ BitTorrent Client

A high-performance, production-grade BitTorrent client written from scratch in modern C++ (C++20). This engine leverages a multi-threaded asynchronous architecture to manage dozens of peer connections simultaneously on a lock-free, non-blocking I/O event loop.

Designed with core systems programming principles in mind, this project demonstrates custom binary protocol parsing, thread-safe memory management, network optimization, and decoupled architecture—making it an ideal piece for deep systems engineering technical portfolios.

---

## 🏗️ System Architecture

The client utilizes a hybrid **Proactor Asynchronous I/O pattern** combined with dedicated component isolation to achieve high throughput without risking lock contention or CPU thread thrashing.

# Asynchronous Multi-Threaded C++ BitTorrent Client

A high-performance, production-grade BitTorrent client written from scratch in modern C++ (C++20). This engine leverages a multi-threaded asynchronous architecture to manage dozens of peer connections simultaneously on a lock-free, non-blocking I/O event loop.

---

## 🏗️ System Architecture

```
        ┌─────────────────────────────────────────────────────────────────┐
        │                    BitTorrent Architecture                      │
        └─────────────────────────────────────────────────────────────────┘

                             ┌───────────────────────┐
                             │     .torrent File     │
                             └──────────┬────────────┘
                                        │
                                        ▼ [Parsed via Bencode]
                             ┌───────────────────────┐
                             │   PieceManager State  │
                             │ (Protected via Mutex) │
                             └──────────┬────────────┘
                                        │
                ┌───────────────────────┼───────────────────────┐
                │                       │                       │
                ▼                       ▼                       ▼
        ┌───────────────┐     ┌───────────────────┐     ┌───────────────┐
        │Tracker Client │     │Network Thread Pool│     │DiskWriter Loop│
        │(HTTP/UDP Port)│     │(io_context::run)  │     │(Task Queue)   │
        └───────────────┘     └─────────┬─────────┘     └───────▲───────┘
                                        │                       │
                ┌───────────────────────┴───────────────────────┤
                ▼                       ▼                       ▼
        ┌───────────────┐     ┌───────────────────┐     ┌───────────────┐
        │Peer Connection│     │  Peer Connection  │     │Peer Connection│
        │   (Strand 1)  │     │    (Strand 2)     │     │   (Strand N)  │
        └───────┬───────┘     └─────────┬─────────┘     └───────┬───────┘
                │                       │                       │
                └───────────────────────┴───────────────────────┘
                                        │
                                        ▼ [Verified Piece Blocks]
                          Pushed to Disk Task Queue
```



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

```
┌─────────────────────────────────────────────────────────────────┐
        │                     BitTorrent Client Flow                      │
        └─────────────────────────────────────────────────────────────────┘

                                   ┌──────────────┐
                                   │  User Input  │
                                   │(Torrent File)│
                                   └──────┬───────┘
                                          │
                                          ▼
                             ┌──────────────────────────┐
                             │ Parse File & Metadata    │
                             │ (bencode::parser module) │
                             └────────────┬─────────────┘
                                          │
                             ┌────────────┴────────────┐
                             │                         │
                             ▼                         ▼
                   ┌───────────────────┐     ┌───────────────────┐
                   │Extract File Specs │     │Calculate InfoHash │
                   │(Size, Pieces, etc)│     │(SHA-1 Crypto Check)│
                   └─────────┬─────────┘     └───────────────────┘
                             │
                             ▼
                   ┌───────────────────┐
                   │  Contact Tracker  │
                   │(tracker_client.cpp)│
                   │ Get Valid Peer IPs│
                   └─────────┬─────────┘
                             │
                             ▼
                   ┌───────────────────┐
                   │Boot Asio Core Pool│
                   │(io_context multi) │
                   └─────────┬─────────┘
                             │
                             ▼
                   ┌───────────────────┐
                   │Establish P2P Conn │
                   │(Strand Isolation) │
                   └─────────┬─────────┘
                             │
               ┌─────────────┼─────────────┐
               ▼             ▼             ▼
         ┌───────────┐ ┌───────────┐ ┌───────────┐
         │Block Req 1│ │Block Req 2│ │Block Req N│
         └─────┬─────┘ └─────┬─────┘ └─────┬─────┘
               │             │             │
               └────────────┬┴────────────┬┘
                            ▼             ▼
                   ┌───────────────────┐
                   │ Verify Hash & Save│
                   │ (DiskWriter Loop) │
                   └─────────┬─────────┘
                             │
                             ▼
                   ┌───────────────────┐
                   │ Update UI Status  │
                   │ (Console Metrics) │
                   └───────────────────┘
```

## ✨ Features

* 

---

## 🎥 Demo

* 

---

## 🚀 How To Use

*