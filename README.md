# Peer-to-Peer Distributed File Sharing System

## Assignment Overview
**Course:** AOS (Advanced Operating Systems)  
**Assignment:** 3  
**Student ID:** 2025201031  
**Institution:** International Institute of Information Technology, Hyderabad  
**Semester:** Monsoon 2025  

This project implements a distributed peer-to-peer file sharing system that allows users to form groups and share files within those groups. The system operates on a peer-to-peer model where files are distributed across participating clients, enabling faster downloads and better resource utilization.

## Table of Contents
1. [System Architecture](#system-architecture)
2. [Compilation Instructions](#compilation-instructions)
3. [Execution Instructions](#execution-instructions)
4. [Core Features](#core-features)
5. [Network Protocol Design](#network-protocol-design)
6. [Data Structures](#data-structures)
7. [Key Algorithms](#key-algorithms)
8. [Implementation Details](#implementation-details)
9. [Testing Procedures](#testing-procedures)
10. [Limitations and Assumptions](#limitations-and-assumptions)
11. [File Structure](#file-structure)

## System Architecture

### Component Overview
The system consists of three main components:

1. **Tracker System** (`tracker/`)
   - Two tracker servers for redundancy and fault tolerance
   - Maintains synchronized state across both trackers
   - Handles user authentication, group management, and peer discovery
   - Primary-backup synchronization mechanism

2. **Client System** (`client/`)
   - User interface for file sharing operations
   - Acts as both downloader (requesting files) and seeder (providing files)
   - Multi-threaded download support with piece-based transfers
   - Built-in uploader server for serving file pieces to other peers

3. **Common Components** (`common/`)
   - Shared protocol definitions and utilities
   - SHA1 hashing implementation for file integrity verification

### Architecture Diagram
```
┌─────────────────┐  sync (port+100)  ┌─────────────────┐
│   Tracker 1     │◄─────────────────►│   Tracker 2     │
│   (Primary)     │  full state +     │   (Backup)      │
│                 │  live UPDATEs     │ takes over if   │
└────────┬────────┘                   │ primary dies    │
         │ commands                   └─────────────────┘
         │ (clients talk only to the current primary)
   ┌─────┴────────────────┐
   │                      │
┌──▼─────────┐  GET_PIECE ┌▼───────────┐
│  Client A  │◄──────────►│  Client B  │
│ (Seeder/   │            │ (Seeder/   │
│ Downloader)│            │ Downloader)│
└────────────┘            └────────────┘
```

## Compilation Instructions

### Prerequisites
- **Compiler:** g++ with C++11 support
- **Libraries:** OpenSSL development libraries
- **Operating System:** Linux (tested on Ubuntu/Debian)

### Install Dependencies
```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install build-essential libssl-dev

# CentOS/RHEL
sudo yum install gcc-c++ openssl-devel

# or for newer versions
sudo dnf install gcc-c++ openssl-devel
```

### Build Process

#### 1. Compile Tracker
```bash
cd tracker/
make clean
make
```

#### 2. Compile Client
```bash
cd client/
make clean
make
```

### Build Output
- `tracker/tracker` - Tracker server executable
- `client/client` - Client application executable

## Execution Instructions

### 1. Setup Tracker Configuration
Create `tracker/tracker_info.txt` with tracker details, one tracker per line
(`IP PORT` or `IP:PORT` both work):
```
127.0.0.1 5000
127.0.0.1 5001
```
Each tracker also uses `PORT + 100` (5100 / 5101 here) for tracker-to-tracker sync.

### 2. Start Tracker Servers

#### Primary Tracker (Terminal 1)
```bash
cd tracker/
./tracker tracker_info.txt 1
```

#### Backup Tracker (Terminal 2)
```bash
cd tracker/
./tracker tracker_info.txt 2
```

### 3. Start Client Applications

#### Client 1 (Terminal 3)
```bash
cd client/
./client 127.0.0.1:7001 ../tracker/tracker_info.txt
```

#### Client 2 (Terminal 4)
```bash
cd client/
./client 127.0.0.1:7002 ../tracker/tracker_info.txt
```

### 4. Basic Usage Workflow

#### User Registration and Login
```bash
# Register a new user
create_user user1 password123

# Login
login user1 password123
```

#### Group Operations
```bash
# Create a group
create_group group1

# List available groups
list_groups

# Join a group (from another client)
join_group group1

# Accept join requests (group owner)
list_requests group1
accept_request group1 user2
```

#### File Operations
```bash
# Upload/Share a file
upload_file group1 /path/to/file.txt

# List files in group
list_files group1

# Download a file (destination can be a directory or a full file path)
download_file group1 file.txt /path/to/destination/

# Show download progress
show_downloads

# Stop sharing a file
stop_share group1 file.txt
```

## Core Features

### Implemented Features
- ✅ **User Management**: Registration, authentication, session management
- ✅ **Group Management**: Create, join, leave groups with owner permissions
- ✅ **File Upload**: Share files with SHA1 integrity verification
- ✅ **File Download**: Multi-threaded piece-based downloads
- ✅ **Peer Discovery**: Automatic peer detection through tracker
- ✅ **File Integrity**: SHA1 verification at piece and file level
- ✅ **Concurrent Downloads**: Multiple simultaneous file downloads
- ✅ **Tracker Synchronization**: Primary-backup tracker redundancy
- ✅ **Persistent Seeding**: Files remain available after upload
- ✅ **Download Progress**: Real-time download status monitoring

### Key Capabilities
- **Large File Support**: Files divided into 512KB pieces for efficient transfer
- **Fault Tolerance**: Automatic piece retry with different peers
- **Load Balancing**: Random peer selection for download distribution
- **Memory Efficiency**: Streaming file processing without loading entire files
- **Thread Safety**: Mutex-protected shared data structures

## Network Protocol Design

### Message Format
All network communication uses TCP sockets with custom text-based protocols.

#### Client-Tracker Protocol
```
Request Format: <COMMAND> <PARAMETERS>
Response Format: <STATUS> <DATA>

Status Codes:
- OK: Success
- ERR <message>: Failure with a human-readable reason

Every tracker response is terminated with `\n\0`, so multi-line replies
(`list_groups`, `list_files`, ...) are always read in full.
```

#### Example Messages
```bash
# User creation
Client → Tracker: "create_user john password123"
Tracker → Client: "OK User created: john"

# File download request
Client → Tracker: "download_file group1 file.txt"
Tracker → Client: "OK DOWNLOAD file.txt 1048576 2 <full_sha1> group1 <piece0_sha1> <piece1_sha1> PEER 192.168.1.1:7001 PEER ..."
                  (only peers whose user is currently logged in are listed)

# Piece request
Client → Peer: "GET_PIECE file.txt 0"
Peer → Client: <raw piece bytes>   (or "ERR <reason>")
```

#### Peer-to-Peer Protocol
```
GET_PIECE <filename> <piece_index>
Response: <raw piece bytes, 512 KB or less for the last piece> | ERR <reason>
(one TCP connection per piece request)
```

### Error Codes
These constants are defined in `common/include/protocol.h`. Replies carry a
readable message (for example `ERR You are not a member of group g1`); only
`ERR_INTERNAL` (500) is put on the wire, by `create_user` and for unknown commands.
```cpp
#define ERR_USER_EXISTS 101
#define ERR_INVALID_LOGIN 102
#define ERR_NOT_LOGGED_IN 103
#define ERR_GROUP_EXISTS 201
#define ERR_GROUP_NOT_FOUND 202
#define ERR_NOT_GROUP_OWNER 203
#define ERR_FILE_NOT_FOUND 301
#define ERR_FILE_ALREADY_SHARED 302
#define ERR_INTERNAL 500
```

## Data Structures

### Core Data Structures

#### 1. SystemState (Tracker)
```cpp
class SystemState {
    map<string, User> users;                              // userId -> password, loggedIn
    map<string, Group> groups;                            // groupId -> owner, members,
                                                          //   pendingRequests, memberOrder
    map<string, map<string, FileMetadata>> groupFiles;    // groupId -> fileName -> metadata
    map<string, string> userSessions;                     // userId -> "IP:PORT" of its seeder
};

struct FileMetadata {        // tracker side
    string groupId, fileName, fullHash;
    size_t fileSize, numChunks;
    vector<string> pieceHashes;
    set<string> owners;      // users sharing the file
    set<string> activePeers; // filled per request from owners who are logged in
};
```

#### 2. DownloadMetadata (Client)
```cpp
struct DownloadMetadata {
    string fileName;
    size_t fileSize;
    size_t numChunks;
    string fullHash;                    // SHA1 of complete file
    string groupId;
    vector<string> pieceHashes;         // SHA1 of each piece
    vector<string> peers;               // Available peer addresses
};
```

#### 3. DownloadContext (Client)
```cpp
struct DownloadContext {
    queue<size_t> workQueue;            // Pending piece downloads
    mutex queueMutex;                   // Thread synchronization
    map<size_t, int> retryCount;        // Per-piece retry tracking
    vector<bool> pieceDone;             // Completion status
    size_t piecesCompleted;             // Progress counter
    mt19937 rng;                        // Random peer order
    string owner;                       // User who started the download
};
```

### Design Rationale

#### Ordered Maps for Lookup
- **User/Group/File Management**: `std::map` gives O(log n) lookups and sorted output for `list_groups` / `list_files`
- **Session Management**: per-connection `ClientSession` (keyed by socket) tells the tracker who is logged in on each connection

#### Thread-Safe Collections
- **Mutex Protection**: All shared data structures protected with appropriate mutexes
- **Lock Guards**: RAII-based locking to prevent deadlocks

#### Memory-Efficient File Handling
- **Streaming Processing**: Files processed in 512KB chunks to minimize memory usage
- **Vector<bool>**: Space-efficient piece completion tracking

## Key Algorithms

### 1. File Chunking Algorithm
```cpp
const size_t CHUNK = 512 * 1024;  // 512KB pieces

// Divide file into pieces; each piece is filled completely even if read()
// returns short counts, so piece boundaries always fall on 512 KB multiples
while (true) {
    size_t got = 0;
    while (got < CHUNK && (n = read(fd, buf + got, CHUNK - got)) > 0) got += n;
    if (got == 0) break;
    computeSHA1(buf, got, hashHex);          // SHA1 of this piece
    pieceHashes.push_back(string(hashHex));
    SHA1_Update(&fullCtx, buf, got);         // running SHA1 of the whole file
    if (got < CHUNK) break;                  // last (short) piece
}
```

**Rationale**: 512KB provides good balance between network efficiency and memory usage.

### 2. Multi-threaded Download Strategy
```cpp
// Up to 8 worker threads run this loop on a shared queue of piece indexes
while (pop pieceIndex from workQueue) {          // under queueMutex
    shuffle(peers);                               // random peer order per piece
    for (peer : peers) {
        // connect, GET_PIECE, receive, check SHA1, pwrite() at pieceIndex * 512 KB
        if (requestPiece(peer, fileName, pieceIndex, expectedHash, expectedSize, fd)) {
            pieceDone[pieceIndex] = true; piecesCompleted++;
            break;
        }
    }
    if (no peer succeeded && ++retryCount[pieceIndex] <= 3)
        workQueue.push(pieceIndex);               // try again later
}
// afterwards: all pieces done? -> verify whole-file SHA1 -> register as seeder
// otherwise the partial file is deleted
```

**Features**:
- **Random Peer Selection**: Distributes load across available peers
- **Automatic Retry**: Failed pieces automatically retried with different peers
- **Concurrent Downloads**: Multiple worker threads download different pieces simultaneously

### 3. Tracker Synchronization Algorithm
```cpp
// Startup: try to connect to the other tracker's sync port (client port + 100).
//   connected  -> BACKUP: send SYNC_REQUEST, load the full SYNC_RESPONSE snapshot,
//                 then apply every "UPDATE ..." line the primary sends
//   refused    -> PRIMARY: listen for clients and for a backup
//
// Primary, after every successful state change:
void TrackerSync::sendUpdate(const string &update) {   // e.g. "LOGIN bob 127.0.0.1:7002"
    if (tracker.getRole() != Tracker::PRIMARY) return;
    updateLog.push_back(update);
    sendMessage("UPDATE " + update);                   // under sendMutex
}
// Backup: applyUpdate() replays CREATE_USER, LOGIN, LOGOUT, GROUP_CREATE, JOIN_GROUP,
//         ACCEPT_REQUEST, LEAVE_GROUP, UPLOAD_FILE, STOP_SHARE on its own SystemState.
// Sync connection closes -> backup promotes itself to PRIMARY and starts serving clients;
//                           a restarted tracker then joins as the new BACKUP.
```

**Features**:
- **Immediate Sync**: every change is sent to the backup as soon as it is applied
- **Full Snapshot on Join**: a (re)starting backup first copies the whole state, so it can join at any time
- **Consistency**: the snapshot is taken and sent under the same lock as updates, so no update is lost or reordered
- **Client Failover**: clients detect the lost connection, find the new primary and log in again automatically

### 4. Piece Selection Strategy
- **In-order Queue**: pieces are queued in order and handed to 8 parallel workers
- **Load Distribution**: Random peer selection spreads network load
- **Retry Logic**: Failed pieces automatically retried up to maximum attempts

## Implementation Details

### Thread Safety Mechanisms
```cpp
// Mutexes for shared resources
mutex stateMutex;          // tracker: users, groups, files, sessions (SystemState)
mutex sessionMutex;        // tracker: per-connection login sessions
mutex sendMutex;           // tracker: writes on the sync connection
mutex downloadsMutex;      // client: active downloads (show_downloads)
mutex seedingFilesMutex;   // client: files this user is sharing
mutex trackerIOMutex;      // client: one request/response on the tracker socket at a time
mutex queueMutex;          // client: per-download piece queue and progress
```

### File Integrity Verification
- **Piece-level Verification**: Each 512KB piece verified with SHA1 hash
- **File-level Verification**: Complete file hash verified after download
- **Automatic Retry**: Corrupted pieces automatically re-downloaded

### Network Error Handling
```cpp
// Robust socket operations with timeout
struct timeval timeout;
timeout.tv_sec = 8;
timeout.tv_usec = 0;
setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
```

### Memory Management
- **RAII Principles**: `lock_guard` for all locking, `vector` for buffers
- **Vector for Buffers**: Automatic memory management for file buffers
- **Stream Processing**: Files processed without loading entirely into memory

## Testing Procedures

> The complete step-by-step test plan, with the exact commands and expected
> output for every feature, is in **[run.md](run.md)**.

### 1. Unit Testing

#### Tracker Functionality
```bash
# Test user management
create_user test1 pass1
create_user test1 pass1  # Should fail (duplicate)
login test1 pass1
login test1 wrongpass   # Should fail

# Test group management
create_group testgroup
create_group testgroup  # Should fail (duplicate)
list_groups
```

#### Client Functionality
```bash
# Test file operations
upload_file group1 testfile.txt
list_files group1
download_file group1 testfile.txt /tmp/downloaded.txt
# Verify downloaded file integrity
```

### 2. Integration Testing

#### Multi-Client Scenario
1. Start 2 tracker servers
2. Start 3+ clients
3. Create users on different clients
4. Create groups and test join/leave operations
5. Upload files from one client, download from others
6. Verify file integrity across all downloads

#### Large File Testing
```bash
# Create test files of various sizes
dd if=/dev/zero of=small.txt bs=1K count=100      # 100KB
dd if=/dev/zero of=medium.txt bs=1M count=10      # 10MB
dd if=/dev/zero of=large.txt bs=1M count=100      # 100MB

# Test upload/download for each size
upload_file group1 large.txt
download_file group1 large.txt /tmp/large_copy.txt
```

### 3. Stress Testing

#### Concurrent Downloads
Downloads run in the background, so type several in a row at the `client>` prompt:
```
download_file group1 file1.txt /tmp/file1.txt
download_file group1 file2.txt /tmp/file2.txt
download_file group1 file3.txt /tmp/file3.txt
show_downloads
```

#### Network Fault Simulation
- Disconnect clients during downloads
- Kill and restart tracker servers
- Test recovery mechanisms

### 4. Performance Testing

#### Throughput Measurement
Timestamps of the first and last piece of a download are in `downloads.log`:
```bash
grep "large.txt piece" downloads.log | sed -n '1p;$p'
```

#### Memory Usage Monitoring
```bash
# Monitor memory usage during large file operations
top -p $(pgrep client)
```

## Limitations and Assumptions

### Current Limitations
1. **Both Trackers Down**: System unavailable if both trackers fail at once (one failing is handled: the backup takes over and clients reconnect and re-login automatically)
2. **No Resume Support**: Downloads must restart if interrupted
3. **Retry Limits**: Each piece is tried up to 4 times across all peers before the download fails
4. **Sequential Piece Selection**: No advanced piece selection algorithms
5. **Memory Usage**: Large file lists may consume significant memory
6. **No Encryption**: Network communication not encrypted; passwords are stored and sent in plain text
7. **Limited Scalability**: Performance may degrade with many concurrent users
8. **Names**: file names and paths given to commands cannot contain spaces
9. **One name per user**: a user cannot seed two different files with the same name in two different groups
10. **In-memory tracker state**: users and groups are lost if both trackers stop

### Assumptions Made
1. **Reliable Local Network**: Designed for LAN environments
2. **Trusted Users**: No protection against malicious peers
3. **File System Access**: Clients have appropriate file permissions
4. **Static IP Addresses**: Peer addresses don't change during sessions
5. **Sufficient Disk Space**: Clients have adequate storage for downloads
6. **Network Stability**: Reasonable network connectivity assumed

### Future Enhancements
- **Encryption**: Add TLS/SSL for secure communication
- **Resume Support**: Allow interrupted downloads to resume
- **Advanced Piece Selection**: Implement rarest-first or similar algorithms
- **DHT Integration**: Reduce dependency on centralized trackers
- **Bandwidth Throttling**: Add configurable upload/download limits
- **Web Interface**: Browser-based client interface

## File Structure

```
2025201031_A3_2/
├── README.md                    # This file
├── run.md                       # Step-by-step test plan for every feature
├── client/                      # Client application
│   ├── Makefile                # Build configuration
│   ├── include/                # Header files
│   │   ├── client.h           # Client class definition
│   │   ├── commands.h         # Command processing
│   │   ├── downloader.h       # Download functionality
│   │   ├── seeds.h            # Seeding management
│   │   ├── uploader.h         # Upload functionality
│   │   └── uploader_server.h  # Peer server
│   └── src/                   # Source files
│       ├── main.cpp           # Client entry point
│       ├── client.cpp         # Network communication
│       ├── commands.cpp       # Command implementation
│       ├── downloader.cpp     # Download logic
│       ├── uploader.cpp       # Upload logic
│       ├── uploader_server.cpp # Peer serving
│       └── seeds.cpp          # Seed management
├── tracker/                    # Tracker server
│   ├── Makefile               # Build configuration
│   ├── tracker_info.txt       # Tracker configuration
│   ├── include/               # Header files
│   │   ├── tracker.h          # Main tracker class
│   │   ├── SystemState.h      # State management
│   │   └── sync.h             # Synchronization
│   └── src/                   # Source files
│       ├── main.cpp           # Tracker entry point
│       ├── tracker.cpp        # Main tracker logic
│       ├── SystemState.cpp    # State implementation
│       └── sync.cpp           # Sync implementation
└── common/                     # Shared components
    ├── include/               # Shared headers
    │   ├── protocol.h         # Protocol definitions
    │   └── sha1.h             # Hash functions
    └── src/                   # Shared source
        └── sha1.cpp           # Hash implementation

```

---

**Note**: This implementation follows the AOS Assignment 3 specifications for a peer-to-peer distributed file sharing system. For detailed technical analysis and performance evaluation, refer to the accompanying technical report and performance analysis documents.
