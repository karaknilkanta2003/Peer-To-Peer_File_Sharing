#ifndef UPLOADER_H
#define UPLOADER_H

#include <string>
#include <vector>
#include <map>
#include <mutex>
using namespace std;

struct FileMetadata {
    string fileName;
    string filePath;
    size_t fileSize;
    size_t numChunks;
    string fullHash;
    vector<string> pieceHashes;
};

struct SeedInfo {
    string path;
    string groupId;
};

extern map<string, map<string, SeedInfo>> userSeeds;

extern mutex seedingFilesMutex;

// An upload that has been announced to the tracker but not yet confirmed.
// The seed is only recorded once the tracker replies OK.
struct PendingUpload {
    bool active = false;
    string userId;
    string fileName;
    string path;
    string groupId;
};

// Hashes the file and sends upload_file to the tracker. Does NOT read the
// reply; on success fills `out` so the caller can commitUpload() after an OK.
// nameOverride registers the file under a different name (used when a
// downloaded copy was saved under another name).
bool uploadFile(class Client &client,
                const string &filePath,
                const string &groupId,
                PendingUpload &out,
                const string &nameOverride = "");

// Records the file as seeded (in memory and in the seeds file).
void commitUpload(const PendingUpload &pu);

#endif
