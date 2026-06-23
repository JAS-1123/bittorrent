# BitTorrent Client (C++)

A highly efficient, lightweight BitTorrent client written from scratch in C++. The implementation relies exclusively on native Linux system components (`epoll`) and keeps external dependencies to an absolute minimum, utilizing only **OpenSSL** for cryptographic SHA-1 piece validation.

---

## 🚀 Features

- **Asynchronous I/O Engine**: Utilizes Linux-specific `epoll` to multiplex and manage hundreds of peer TCP connections concurrently within a single network thread.
- **Multi-Threaded Architecture**: Separates networking, heavy disk I/O operations, and speed calculations across isolated worker threads to eliminate execution bottlenecks.
- **Dual-Tracker Protocol Support**: Full capability to parse and communicate with both HTTP/TCP and UDP trackers.
- **Rarest-First Piece Selection**: Implements a synchronized priority queue tracking peer `bitfields` to prioritize downloading the rarest pieces first, maximizing swarm download efficiency.
- **Robust Bencode Parser**: Built-in native decoder capable of parsing complex nested bencoded `.torrent` structures into typed memory objects.
- **Smooth Speed Calculation**: Evaluates real-time throughput metrics via an independent thread executing an exponential decay mathematical model.

---

## 🏗️ Architecture Design

The client separates concerns into distinct modules driven by an event-driven runtime combined with worker pools:

```
                  ┌────────────────────────────────────────┐
                  │          Main Client Thread            │
                  └───────┬────────────────────────┬───────┘
                          │                        │
                          ▼                        ▼
 ┌──────────────────────────────────────┐  ┌─────────────────────────────────┐
 │       Network Thread (epoll)         │  │    Telemetry Worker Thread      │
 │ ──────────────────────────────────── │  │ ─────────────────────────────── │
 │  - Multiplexes Peer TCP Sockets      │  │  - Monitors transfer byte counts│
 │  - Processes Wire Protocol Messages  │  │  - Computes real-time speeds    │
 │  - Manages Peer Handshakes/Choking   │  │    via exponential decay        │
 └──────────────────┬───────────────────┘  └─────────────────────────────────┘
                    │
                    │ (Pushes data blocks)
                    ▼
 ┌──────────────────────────────────────┐
 │         Disk Worker Thread           │
 │ ──────────────────────────────────── │
 │  - Consumes safe memory buffers      │
 │  - Offloads blocking I/O from epoll  │
 │  - Verifies OpenSSL SHA-1 hashes     │
 └──────────────────────────────────────┘
```

### Architectural Component Breakdown:
1. **Parser & Bencode Decoder**: Takes raw torrent byte streams and structures them into accessible maps, integers, strings, and lists.
2. **Tracker Client**: Resolves the main tracker endpoint, safely handles connection responses, and extracts initial peer `(IP, Port)` address pairs.
3. **The Epoll Event Loop**: Operates on a single thread. It polls all connected peer sockets using `epoll_wait()`. When bytes arrive, they are consumed according to the BitTorrent wire-protocol state machine (`Handshake`, `Bitfield`, `Have`, `Unchoke`, `Piece`).
4. **Disk IO Syncer**: A secondary thread acting as a consumer to the data buffers filled by the network thread. It serializes file allocations and raw block writes directly to storage.

---

## 🛠️ Compilation & Installation

### Prerequisites

Ensure your system has a modern C++ compiler (`gcc` or `clang`), `cmake`, and `OpenSSL` development headers installed.

#### 🔹 Arch Linux Setup
```bash
sudo pacman -S base-devel cmake openssl
```

#### 🔹 Ubuntu / Debian Setup
```bash
sudo apt update
sudo apt install build-essential cmake libssl-dev
```

### Building the Project

1. Clone the repository and navigate to its root directory:
```bash
git clone https://github.com/JAS-1123/bittorrent.git
cd bittorrent
```

2. Generate the build files and compile using CMake:
```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

---

## 💻 Usage

To execute the client and begin downloading a torrent payload, point the compiled executable to a target `.torrent` file:    

```bash
./build/BitTorrent /path/to/your/target.torrent
```

### Running Unit Tests
The project utilizes GoogleTest for verification. To compile and execute the test binary suite:
```bash
# From the root directory
mkdir -p build && cd build
cmake ..
make
./test/BitTorrent_test
```

---

## 🧪 Testing (Arch Linux Environment)

To verify the engine works locally, you can test it by downloading a valid single-file torrent (like the Debian netinst ISO) and running the compiled engine against it.

```bash
# 1. Download a valid test torrent
wget -q "https://cdimage.debian.org/debian-cd/current/amd64/bt-cd/debian-13.5.0-amd64-netinst.iso.torrent" -O test.torrent

# 2. Build the project
mkdir -p build && cd build
cmake ..
make
cd ..

# 3. Run the engine against the torrent
./build/BitTorrent test.torrent
```