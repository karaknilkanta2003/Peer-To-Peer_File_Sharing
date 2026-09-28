#include "../../common/include/sha1.h"
#include "../../common/include/protocol.h"
#include "../include/client.h"
#include "../include/seeds.h"
#include "../include/uploader_server.h"
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

using namespace std;

bool uploadFile(Client &client, const string &filePath, const string &groupId) {
    struct stat st;
    if (stat(filePath.c_str(), &st) < 0) {
        cerr << "[UPLOAD] Could not stat file: " << filePath
             << " (" << strerror(errno) << ")" << endl;
        return false;
    }
    size_t totalSize = st.st_size;

    int fd = open(filePath.c_str(), O_RDONLY);
    if (fd < 0) {
        cerr << "[UPLOAD] Could not open file: " << filePath
             << " (" << strerror(errno) << ")" << endl;
        return false;
    }

    const size_t CHUNK = 512 * 1024;
    vector<string> pieceHashes;
    char *buf = new char[CHUNK];
    ssize_t n;

    SHA_CTX fullCtx;
    SHA1_Init(&fullCtx);

    while ((n = read(fd, buf, CHUNK)) > 0) {
        char hashHex[41];
        computeSHA1(buf, n, hashHex);
        pieceHashes.push_back(string(hashHex));

        SHA1_Update(&fullCtx, buf, n);
    }

    close(fd);
    delete[] buf;

    unsigned char digest[20];
    SHA1_Final(digest, &fullCtx);
    char fullHashHex[41];
    for (int i = 0; i < 20; i++) sprintf(fullHashHex + (i * 2), "%02x", digest[i]);
    fullHashHex[40] = '\0';
    string fullHash(fullHashHex);

    string fileName = filePath.substr(filePath.find_last_of("/\\") + 1);

    ostringstream msg;
    msg << CMD_UPLOAD_FILE << " "
        << groupId << " "
        << fileName << " "
        << totalSize << " "
        << pieceHashes.size() << " "
        << fullHash;
    for (const auto &h : pieceHashes) msg << " " << h;

    client.sendMessage(msg.str() + "\n");

    cout << "[UPLOAD] Metadata sent to tracker: "
         << fileName << " (" << totalSize << " bytes, "
         << pieceHashes.size() << " pieces, seeder="
         << client.getUserId() << ")" << endl;

    string userId = client.getUserId();
    auto seeds = loadSeeds(userId);

    SeedInfo info;
    info.path = filePath;
    info.groupId = groupId;

    seeds[fileName] = info;
    saveSeeds(userId, seeds);

    {
        lock_guard<mutex> lock(seedingFilesMutex);
        userSeeds[userId][fileName] = info;
    }

    cout << "[DEBUG] Seeding " << fileName
         << " from path " << filePath
         << " in group " << groupId
         << " for user " << userId << "\n";

    return true;
}
