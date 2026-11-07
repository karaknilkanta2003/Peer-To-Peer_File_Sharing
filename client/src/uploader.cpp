#include "../../common/include/sha1.h"
#include "../../common/include/protocol.h"
#include "../include/client.h"
#include "../include/seeds.h"
#include "../include/uploader_server.h"
#include "../include/uploader.h"
#include <iostream>
#include <fcntl.h>
#include <cstring>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>
#include <vector>
#include <string>
#include <sstream>
#include <openssl/sha.h>
#include <map>
#include <mutex>
#include <climits>
#include <cstdlib>

using namespace std;

bool uploadFile(Client &client, const string &filePath, const string &groupId,
                PendingUpload &out, const string &nameOverride) {
    out = PendingUpload();

    string userId = client.getUserId();
    if (userId.empty()) {
        cerr << "[UPLOAD] Not logged in." << endl;
        return false;
    }

    struct stat st;
    if (stat(filePath.c_str(), &st) < 0) {
        cerr << "[UPLOAD] Could not stat file: " << filePath
             << " (" << strerror(errno) << ")" << endl;
        return false;
    }
    if (!S_ISREG(st.st_mode)) {
        cerr << "[UPLOAD] Not a regular file: " << filePath << endl;
        return false;
    }
    if (st.st_size == 0) {
        cerr << "[UPLOAD] Empty files cannot be shared: " << filePath << endl;
        return false;
    }
    size_t totalSize = st.st_size;

    // Store an absolute path so seeding still works if the client is later
    // restarted from a different directory.
    string absPath = filePath;
    char resolved[PATH_MAX];
    if (realpath(filePath.c_str(), resolved)) absPath = resolved;

    int fd = open(absPath.c_str(), O_RDONLY);
    if (fd < 0) {
        cerr << "[UPLOAD] Could not open file: " << filePath
             << " (" << strerror(errno) << ")" << endl;
        return false;
    }

    const size_t CHUNK = 512 * 1024;
    vector<string> pieceHashes;
    vector<char> buf(CHUNK);

    SHA_CTX fullCtx;
    SHA1_Init(&fullCtx);

    // Fill each piece completely: read() may return short counts, and a
    // short piece would shift every later piece boundary.
    while (true) {
        size_t got = 0;
        while (got < CHUNK) {
            ssize_t n = read(fd, buf.data() + got, CHUNK - got);
            if (n < 0) {
                if (errno == EINTR) continue;
                cerr << "[UPLOAD] Read failed: " << strerror(errno) << endl;
                close(fd);
                return false;
            }
            if (n == 0) break;
            got += (size_t)n;
        }
        if (got == 0) break;

        char hashHex[41];
        computeSHA1(buf.data(), got, hashHex);
        pieceHashes.push_back(string(hashHex));
        SHA1_Update(&fullCtx, buf.data(), got);

        if (got < CHUNK) break;
    }
    close(fd);

    unsigned char digest[20];
    SHA1_Final(digest, &fullCtx);
    char fullHashHex[41];
    for (int i = 0; i < 20; i++) sprintf(fullHashHex + (i * 2), "%02x", digest[i]);
    fullHashHex[40] = '\0';
    string fullHash(fullHashHex);

    string fileName = nameOverride.empty()
        ? absPath.substr(absPath.find_last_of("/\\") + 1)
        : nameOverride;

    if (fileName.find(' ') != string::npos) {
        cerr << "[UPLOAD] File names with spaces are not supported: " << fileName << endl;
        return false;
    }

    ostringstream msg;
    msg << CMD_UPLOAD_FILE << " "
        << groupId << " "
        << fileName << " "
        << totalSize << " "
        << pieceHashes.size() << " "
        << fullHash;
    for (const auto &h : pieceHashes) msg << " " << h;

    if (!client.sendMessage(msg.str())) {
        cerr << "[UPLOAD] Could not send metadata to tracker" << endl;
        return false;
    }

    cout << "[UPLOAD] Metadata sent to tracker: "
         << fileName << " (" << totalSize << " bytes, "
         << pieceHashes.size() << " pieces)" << endl;

    out.active = true;
    out.userId = userId;
    out.fileName = fileName;
    out.path = absPath;
    out.groupId = groupId;
    return true;
}

void commitUpload(const PendingUpload &pu) {
    if (!pu.active || pu.userId.empty()) return;

    SeedInfo info;
    info.path = pu.path;
    info.groupId = pu.groupId;

    map<string, SeedInfo> snapshot;
    {
        lock_guard<mutex> lock(seedingFilesMutex);
        userSeeds[pu.userId][pu.fileName] = info;
        snapshot = userSeeds[pu.userId];
    }
    saveSeeds(pu.userId, snapshot);

    cout << "[SEEDER] Now sharing " << pu.fileName << " (group " << pu.groupId
         << ", path " << pu.path << ")" << endl;
}
