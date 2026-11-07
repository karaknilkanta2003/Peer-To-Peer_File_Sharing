#include "../include/downloader.h"
#include "../../common/include/sha1.h"
#include "../include/uploader.h"
#include "../include/uploader_server.h"
#include "../include/client.h"
#include <openssl/sha.h>
#include <thread>
#include <mutex>
#include <iostream>
#include <queue>
#include <random>
#include <vector>
#include <sstream>
#include <unistd.h>
#include <algorithm>
#include <fcntl.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <cstring>
#include <cerrno>
#include <map>
#include <cstdio>
#include <ctime>
#include <sys/time.h>

using namespace std;

static const size_t CHUNK = 512 * 1024;

struct DownloadContext;
static mutex downloadsMutex;
static map<string, DownloadContext*> activeDownloads;

static mutex logMutex;
static FILE *downloadLog = fopen("downloads.log", "a");

map<string, map<string, SeedInfo>> userSeeds;

struct CompletedInfo {
    string path;
    string groupId;
};

static map<string, map<string, CompletedInfo>> completedDownloads;

static void logDownloadEvent(const string &msg) {
    lock_guard<mutex> lock(logMutex);
    if (downloadLog) {
        time_t now = time(nullptr);
        char ts[64];
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(downloadLog, "[%s] %s\n", ts, msg.c_str());
        fflush(downloadLog);
    }
}

static bool verifyFullFile(const DownloadMetadata &meta, const string &destPath) {
    int fd = open(destPath.c_str(), O_RDONLY);
    if (fd < 0) {
        logDownloadEvent("[CLIENT] open for verify failed: " + string(strerror(errno)));
        return false;
    }

    const size_t BUF = 512 * 1024;
    vector<char> buf(BUF);
    ssize_t n;

    SHA_CTX ctx;
    if (SHA1_Init(&ctx) != 1) {
        logDownloadEvent("[CLIENT] SHA1_Init failed");
        close(fd);
        return false;
    }

    while ((n = read(fd, buf.data(), BUF)) > 0) {
        if (SHA1_Update(&ctx, reinterpret_cast<unsigned char*>(buf.data()), (size_t)n) != 1) {
            logDownloadEvent("[CLIENT] SHA1_Update failed");
            close(fd);
            return false;
        }
    }
    if (n < 0) {
        logDownloadEvent("[CLIENT] read during verify failed: " + string(strerror(errno)));
        close(fd);
        return false;
    }

    unsigned char digest[SHA_DIGEST_LENGTH];
    if (SHA1_Final(digest, &ctx) != 1) {
        logDownloadEvent("[CLIENT] SHA1_Final failed");
        close(fd);
        return false;
    }

    close(fd);

    char hashHex[41];
    for (int i = 0; i < (int)SHA_DIGEST_LENGTH; ++i) {
        sprintf(hashHex + (i * 2), "%02x", digest[i]);
    }
    hashHex[40] = '\0';

    if (meta.fullHash != string(hashHex)) {
        logDownloadEvent("[CLIENT] Final hash mismatch! Expected " + meta.fullHash + " but got " + string(hashHex));
        return false;
    }

    logDownloadEvent("[CLIENT] Final file hash verified successfully!");
    return true;
}

DownloadMetadata parseDownloadResponse(const string &response) {
    logDownloadEvent("[DEBUG] Full response length: " + to_string(response.length()));
    logDownloadEvent("[DEBUG] Response (truncated): " + response.substr(0, min(response.length(), (size_t)200)));

    DownloadMetadata meta;
    istringstream iss(response);
    string status, keyword;
    iss >> status >> keyword;

    logDownloadEvent("[DEBUG] Status=" + status + " Keyword=" + keyword);

    if (status != "OK" || keyword != "DOWNLOAD") {
        logDownloadEvent("[CLIENT] Invalid download response status/keyword");
        return meta;
    }

    iss >> meta.fileName >> meta.fileSize >> meta.numChunks >> meta.fullHash >> meta.groupId;

    logDownloadEvent("[DEBUG] Parsed: file=" + meta.fileName +
                     " size=" + to_string(meta.fileSize) +
                     " chunks=" + to_string(meta.numChunks) +
                     " hash=" + meta.fullHash +
                     " group=" + meta.groupId);

    meta.pieceHashes.clear();
    meta.pieceHashes.reserve(meta.numChunks);
    for (size_t i = 0; i < meta.numChunks; ++i) {
        string h;
        if (!(iss >> h)) {
            logDownloadEvent("[CLIENT] Incomplete piece hash list (got " +
                             to_string(meta.pieceHashes.size()) + " of " + to_string(meta.numChunks) + ")");
            return DownloadMetadata{};
        }
        meta.pieceHashes.push_back(h);
    }

    meta.peers.clear();
    string tok;
    while (iss >> tok) {
        if (tok == "PEER") {
            string peer;
            if (iss >> peer) meta.peers.push_back(peer);
        }
    }

    if (meta.peers.empty()) {
        logDownloadEvent("[CLIENT] No peers provided for download");
    }

    logDownloadEvent("[DEBUG] Final parsed pieces: " + to_string(meta.pieceHashes.size()) +
                     ", peers: " + to_string(meta.peers.size()));

    return meta;
}

static size_t expectedPieceSize(const DownloadMetadata &meta, size_t pieceIndex) {
    if (meta.numChunks == 0) return 0;
    if (pieceIndex + 1 < meta.numChunks) return CHUNK;
    return meta.fileSize - (meta.numChunks - 1) * CHUNK;
}

static bool requestPiece(const string &peer,
                         const string &fileName,
                         int pieceIndex,
                         const string &expectedHash,
                         size_t expectedSize,
                         int fd) {
    if (expectedSize == 0) return false;

    size_t colon = peer.find(':');
    if (colon == string::npos) return false;

    string ip = peer.substr(0, colon);
    int port = stoi(peer.substr(colon + 1));

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        logDownloadEvent("[CLIENT] socket() failed: " + string(strerror(errno)));
        return false;
    }

    struct timeval tv{8,0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
        close(sock);
        return false;
    }

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        logDownloadEvent("[CLIENT] connect() to " + peer + " failed: " + string(strerror(errno)));
        close(sock);
        return false;
    }

    logDownloadEvent("[CLIENT] Connected to peer " + peer + " for piece " + to_string(pieceIndex));

    string req = "GET_PIECE " + fileName + " " + to_string(pieceIndex) + "\n";
    if (send(sock, req.c_str(), req.size(), 0) < 0) {
        logDownloadEvent("[CLIENT] send() failed: " + string(strerror(errno)));
        close(sock);
        return false;
    }

    vector<uint8_t> buf(expectedSize);
    ssize_t total = 0;
    while (total < (ssize_t)expectedSize) {
        ssize_t r = recv(sock, reinterpret_cast<char*>(buf.data()) + total, expectedSize - total, 0);
        if (r < 0) {
            logDownloadEvent("[CLIENT] recv() error from " + peer + ": " + string(strerror(errno)));
            close(sock);
            return false;
        }
        if (r == 0) break;
        total += r;
    }
    close(sock);

    // Check for an ERR reply before the size check: an error is far shorter
    // than a piece, so a size-first check reports a bare byte count and hides
    // the reason the peer refused.
    if (total >= 3 && buf[0] == 'E' && buf[1] == 'R' && buf[2] == 'R') {
        string err(reinterpret_cast<char*>(buf.data()), (size_t)total);
        while (!err.empty() && (err.back() == '\n' || err.back() == '\r')) err.pop_back();
        logDownloadEvent("[CLIENT] Peer " + peer + " refused piece " +
                         to_string(pieceIndex) + ": " + err);
        return false;
    }

    if ((size_t)total != expectedSize) {
        logDownloadEvent("[CLIENT] Piece " + to_string(pieceIndex) + " from " + peer +
                         " returned " + to_string(total) + " bytes, expected " + to_string(expectedSize));
        return false;
    }

    char hashHex[41];
    computeSHA1(reinterpret_cast<char*>(buf.data()), expectedSize, hashHex);
    if (expectedHash != string(hashHex)) {
        logDownloadEvent("[CLIENT] Hash mismatch for piece " + to_string(pieceIndex) +
                         " from " + peer + " expected " + expectedHash + " got " + hashHex);
        return false;
    }

    off_t offset = (off_t)pieceIndex * CHUNK;
    ssize_t written = 0;
    while (written < (ssize_t)expectedSize) {
        ssize_t w = pwrite(fd, buf.data() + written, expectedSize - written, offset + written);
        if (w < 0) {
            logDownloadEvent("[CLIENT] pwrite failed: " + string(strerror(errno)));
            return false;
        }
        written += w;
    }

    return true;
}

void closeDownloadLog() {
    lock_guard<mutex> lock(logMutex);
    if (downloadLog) {
        fclose(downloadLog);
        downloadLog = nullptr;
    }
}

struct DownloadContext {
    queue<size_t> workQueue;
    mutex queueMutex;
    mt19937 rng;
    const DownloadMetadata *meta;
    int fd;
    map<size_t,int> retryCount;
    vector<bool> pieceDone;
    size_t piecesCompleted = 0;
    string owner;   // user who started the download
};

static void downloadWorker(int tid, DownloadContext *ctx) {
    while (true) {
        size_t pieceIndex;
        {
            lock_guard<mutex> lock(ctx->queueMutex);
            if (ctx->workQueue.empty()) return;
            pieceIndex = ctx->workQueue.front();
            ctx->workQueue.pop();
            if (ctx->pieceDone[pieceIndex]) continue;
        }

        bool success = false;
        vector<string> peers = ctx->meta->peers;
        shuffle(peers.begin(), peers.end(), ctx->rng);

        size_t expectedSize = expectedPieceSize(*ctx->meta, pieceIndex);
        for (auto &peer : peers) {
            if (requestPiece(peer, ctx->meta->fileName, (int)pieceIndex,
                             ctx->meta->pieceHashes[pieceIndex], expectedSize, ctx->fd)) {
                {
                    lock_guard<mutex> qlock(ctx->queueMutex);
                    if (!ctx->pieceDone[pieceIndex]) {
                        ctx->pieceDone[pieceIndex] = true;
                        ++ctx->piecesCompleted;
                    }
                }
                logDownloadEvent("[DOWNLOAD] " + ctx->meta->fileName +
                                 " piece " + to_string(pieceIndex+1) + "/" +
                                 to_string(ctx->meta->numChunks) + " from " + peer);
                success = true;
                break;
            }
        }

        if (!success) {
            lock_guard<mutex> lock(ctx->queueMutex);
            int count = ++ctx->retryCount[pieceIndex];
            if (!ctx->pieceDone[pieceIndex]) {
                if (count <= 3) {
                    ctx->workQueue.push(pieceIndex);
                    logDownloadEvent("[CLIENT] Worker " + to_string(tid) +
                                     " requeued piece " + to_string(pieceIndex) +
                                     " (retry " + to_string(count) + ")");
                } else {
                    logDownloadEvent("[CLIENT] Worker " + to_string(tid) +
                                     " gave up on piece " + to_string(pieceIndex) +
                                     " after " + to_string(count) + " attempts");
                }
            }
        }
    }
}

void showDownloads(const string &userId) {
    bool any = false;

    // Hold downloadsMutex while printing: the download thread erases its
    // entry under this lock before freeing the context, so nothing printed
    // here can be freed underneath us.
    {
        lock_guard<mutex> lock(downloadsMutex);
        for (auto &entry : activeDownloads) {
            DownloadContext *ctx = entry.second;
            if (!ctx || !ctx->meta) continue;
            if (ctx->owner != userId) continue;

            any = true;
            size_t done = 0, total = 0;
            {
                lock_guard<mutex> qlock(ctx->queueMutex);
                done = ctx->piecesCompleted;
                total = ctx->meta->numChunks;
            }
            int percent = (total > 0) ? (int)(100.0 * done / total) : 0;
            cout << "[D] [" << ctx->meta->groupId << "] " << ctx->meta->fileName
                 << " : " << done << "/" << total
                 << " pieces (" << percent << "%) -> " << entry.first << endl;
        }
    }

    map<string, CompletedInfo> completed;
    {
        lock_guard<mutex> slock(seedingFilesMutex);
        auto sit = completedDownloads.find(userId);
        if (sit != completedDownloads.end()) {
            completed = sit->second;
        }
    }

    for (auto &p : completed) {
        any = true;
        cout << "[C] [" << p.second.groupId << "] " << p.first << " -> " << p.second.path << endl;
    }

    if (!any) {
        cout << "[CLIENT] No downloads for user " << userId << endl;
    }
}


bool startDownload(Client &client, const DownloadMetadata &meta, const string &destPath) {
    if (meta.fileName.empty() || meta.numChunks == 0 ||
        meta.pieceHashes.size() != meta.numChunks) {
        cerr << "[CLIENT] Tracker sent an invalid download response" << endl;
        return false;
    }
    if (meta.peers.empty()) {
        cerr << "[CLIENT] No online peer is sharing " << meta.fileName
             << " right now. Try again when a seeder is logged in." << endl;
        return false;
    }

    string userId = client.getUserId();

    DownloadContext *ctx = new DownloadContext();
    DownloadMetadata *metaCopy = new DownloadMetadata(meta);
    ctx->meta = metaCopy;
    ctx->owner = userId;
    ctx->fd = -1;
    ctx->rng.seed(random_device{}());
    ctx->pieceDone.assign(metaCopy->numChunks, false);

    for (size_t i = 0; i < metaCopy->numChunks; ++i) ctx->workQueue.push(i);

    {
        lock_guard<mutex> lock(downloadsMutex);
        if (activeDownloads.count(destPath)) {
            cerr << "[CLIENT] A download to " << destPath << " is already in progress" << endl;
            delete metaCopy;
            delete ctx;
            return false;
        }
        activeDownloads[destPath] = ctx;
    }

    cout << "[CLIENT] Downloading " << meta.fileName << " (" << meta.fileSize << " bytes, "
         << meta.numChunks << " pieces) from " << meta.peers.size()
         << " peer(s) -> " << destPath << endl;

    thread([ctx, metaCopy, destPath, userId, &client]() {
        auto finish = [&]() {
            lock_guard<mutex> lock(downloadsMutex);
            activeDownloads.erase(destPath);
            delete ctx->meta;
            delete ctx;
        };

        int fd = open(destPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd < 0) {
            string err = strerror(errno);
            logDownloadEvent("[CLIENT] open dest failed: " + err);
            cerr << "\n[CLIENT] Cannot create " << destPath << ": " << err << endl;
            finish();
            return;
        }
        if (ftruncate(fd, metaCopy->fileSize) < 0) {
            string err = strerror(errno);
            logDownloadEvent("[CLIENT] ftruncate failed: " + err);
            cerr << "\n[CLIENT] Cannot allocate " << destPath << ": " << err << endl;
            close(fd);
            unlink(destPath.c_str());
            finish();
            return;
        }

        ctx->fd = fd;
        int numThreads = (int)min((size_t)8, metaCopy->numChunks);
        vector<thread> workers;
        for (int i = 0; i < numThreads; ++i) workers.emplace_back(downloadWorker, i, ctx);
        for (auto &t : workers) t.join();

        if (ctx->fd >= 0) { close(ctx->fd); ctx->fd = -1; }

        size_t done;
        {
            lock_guard<mutex> qlock(ctx->queueMutex);
            done = ctx->piecesCompleted;
        }

        // Copy everything the follow-up needs before the context is freed.
        string fileName = metaCopy->fileName;
        string groupId = metaCopy->groupId;
        size_t numChunks = metaCopy->numChunks;

        if (done != numChunks) {
            logDownloadEvent("[CLIENT] Download failed: only " + to_string(done) + " of " +
                             to_string(numChunks) + " pieces of " + fileName +
                             " could be fetched from any peer");
            cerr << "\n[CLIENT] Download of " << fileName << " failed: " << (numChunks - done)
                 << " of " << numChunks << " pieces unavailable from any peer (see downloads.log)\n";
            unlink(destPath.c_str());
        } else if (!verifyFullFile(*metaCopy, destPath)) {
            logDownloadEvent("[CLIENT] Download failed: hash mismatch for " + fileName);
            cerr << "\n[CLIENT] Download of " << fileName << " failed: whole-file SHA1 mismatch\n";
            unlink(destPath.c_str());
        } else {
            cout << "\n[CLIENT] Download complete: " << destPath << " (SHA1 verified)" << endl;
            {
                lock_guard<mutex> lock(seedingFilesMutex);
                completedDownloads[userId][fileName] = {destPath, groupId};
            }

            // Become a seeder for the file under its original name.
            thread([destPath, fileName, groupId, userId, &client]() {
                lock_guard<mutex> io(trackerIOMutex);
                if (client.getUserId() != userId || !client.isConnected()) return;

                PendingUpload pu;
                if (!uploadFile(client, destPath, groupId, pu, fileName)) return;
                string resp = client.receiveMessage();
                if (resp.rfind("OK", 0) == 0) {
                    commitUpload(pu);
                } else {
                    cerr << "[CLIENT] Could not register " << fileName
                         << " as seeded: " << resp << endl;
                }
            }).detach();
        }

        finish();
    }).detach();

    return true;
}
