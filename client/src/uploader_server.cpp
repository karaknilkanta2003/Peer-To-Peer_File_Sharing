#include "../include/uploader.h"
#include "../include/uploader_server.h"
#include "../include/seeds.h"
#include <thread>
#include <vector>
#include <mutex>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <atomic>
#include <sstream>
#include <map>
#include <cstdio>
#include <ctime>
#include <iostream>
using namespace std;

mutex seedingFilesMutex;

static int serverSock = -1;
static atomic<bool> running{false};
static thread acceptThread;
static string serverUserId;

static mutex logMutex;
static FILE *uploadLog = fopen("uploads.log", "a");

static void logUploadEvent(const string &msg) {
    lock_guard<mutex> lock(logMutex);
    if (uploadLog) {
        time_t now = time(nullptr);
        char ts[64];
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(uploadLog, "[%s] %s\n", ts, msg.c_str());
        fflush(uploadLog);
    }
}

static void sendErrAndClose(int sock, const string &msg) {
    send(sock, msg.c_str(), msg.size(), 0);
    close(sock);
}

static void handlePeerRequest(int clientSock, const string &userId) {
    char buffer[1024];
    ssize_t n = recv(clientSock, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
        close(clientSock);
        return;
    }
    buffer[n] = '\0';

    string cmd, fileName;
    long long pieceIndex = -1;
    {
        istringstream iss(buffer);
        iss >> cmd >> fileName >> pieceIndex;
    }

    if (cmd != "GET_PIECE" || fileName.empty() || pieceIndex < 0) {
        logUploadEvent("[SEEDER] Invalid request: " + string(buffer));
        sendErrAndClose(clientSock, "ERR Invalid request\n");
        return;
    }

    string filePath;
    {
        lock_guard<mutex> lock(seedingFilesMutex);
        auto userIt = userSeeds.find(userId);
        if (userIt == userSeeds.end() || userIt->second.find(fileName) == userIt->second.end()) {
            logUploadEvent("[SEEDER] User " + userId + " not seeding file: " + fileName);
            sendErrAndClose(clientSock, "ERR File not available\n");
            return;
        }
        filePath = userIt->second[fileName].path;
    }

    struct stat st;
    if (stat(filePath.c_str(), &st) < 0) {
        logUploadEvent("[SEEDER] stat failed for " + filePath + ": " + string(strerror(errno)));
        sendErrAndClose(clientSock, "ERR Could not stat file\n");
        return;
    }
    off_t totalSize = st.st_size;

    const size_t CHUNK = 512 * 1024;
    off_t offset = pieceIndex * (off_t)CHUNK;

    if (offset < 0 || offset >= totalSize) {
        logUploadEvent("[SEEDER] Invalid piece index " + to_string(pieceIndex) + " for " + fileName);
        sendErrAndClose(clientSock, "ERR Invalid piece index\n");
        return;
    }

    size_t expectedSize = CHUNK;
    if (offset + (off_t)CHUNK > totalSize) {
        expectedSize = (size_t)(totalSize - offset);
    }

    int fd = open(filePath.c_str(), O_RDONLY);
    if (fd < 0) {
        logUploadEvent("[SEEDER] open failed for " + filePath + ": " + string(strerror(errno)));
        sendErrAndClose(clientSock, "ERR Could not open file\n");
        return;
    }

    vector<char> buf(expectedSize);
    ssize_t r = pread(fd, buf.data(), expectedSize, offset);
    close(fd);

    if (r < 0) {
        logUploadEvent("[SEEDER] pread failed for " + filePath + ": " + string(strerror(errno)));
        sendErrAndClose(clientSock, "ERR Read failed\n");
        return;
    }
    if ((size_t)r != expectedSize) {
        logUploadEvent("[SEEDER] short read: expected " + to_string(expectedSize) +
                       " got " + to_string(r));
        sendErrAndClose(clientSock, "ERR Short read\n");
        return;
    }

    size_t sent = 0;
    while (sent < expectedSize) {
        ssize_t s = send(clientSock, buf.data() + sent, expectedSize - sent, 0);
        if (s < 0) {
            if (errno == EINTR) continue;
            logUploadEvent("[SEEDER] send() failed: " + string(strerror(errno)));
            break;
        }
        sent += (size_t)s;
    }

    logUploadEvent("[SEEDER][" + userId + "] Sent piece " +
                   to_string(pieceIndex) + " of " + fileName +
                   " (" + to_string(expectedSize) + " bytes)");
    close(clientSock);
}

bool startUploaderServer(const string &ip, int port, const string &userId) {
    if (running.load()) {
        if (serverUserId == userId) {
            logUploadEvent("[SEEDER] Already running as user " + userId);
            return true;
        }
        // The accept loop captures its userId by value, so a running server can
        // never start serving a different user. Tear it down and rebind, or we
        // would keep answering as the previous user while the tracker
        // advertises this address for the new one.
        logUploadEvent("[SEEDER] User switch: was serving " + serverUserId +
                       ", restarting as " + userId);
        stopUploaderServer();
    }

    serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSock < 0) {
        logUploadEvent("[SEEDER] socket failed: " + string(strerror(errno)));
        return false;
    }

    int opt = 1;
    setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        close(serverSock);
        return false;
    }

    if (bind(serverSock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        logUploadEvent("[SEEDER] bind failed: " + string(strerror(errno)));
        close(serverSock);
        return false;
    }

    if (listen(serverSock, 16) < 0) {
        logUploadEvent("[SEEDER] listen failed: " + string(strerror(errno)));
        close(serverSock);
        return false;
    }

    running = true;
    serverUserId = userId;
    logUploadEvent("[SEEDER] Listening on " + ip + ":" + to_string(port) + " as user " + userId);

    acceptThread = thread([=]() {
        while (running.load()) {
            int clientSock = accept(serverSock, nullptr, nullptr);
            if (clientSock < 0) {
                if (!running.load()) break;
                logUploadEvent("[SEEDER] accept failed: " + string(strerror(errno)));
                continue;
            }
            logUploadEvent("[SEEDER] Incoming connection from peer");
            thread([clientSock, userId]() {
                handlePeerRequest(clientSock, userId);
            }).detach();
        }
        if (serverSock != -1) {
            close(serverSock);
            serverSock = -1;
        }
    });


    return true;
}

void stopUploaderServer() {
    if (!running.load()) return;
    running = false;

    if (serverSock != -1) {
        shutdown(serverSock, SHUT_RDWR);
        close(serverSock);
        serverSock = -1;
    }

    if (acceptThread.joinable()) {
        acceptThread.join();
    }

    logUploadEvent("[SEEDER] Uploader server stopped for user " + serverUserId);
    serverUserId.clear();
}

void closeUploaderLog() {
    lock_guard<mutex> lock(logMutex);
    if (uploadLog) {
        fclose(uploadLog);
        uploadLog = nullptr;
    }
}
