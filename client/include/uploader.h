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

bool uploadFile(class Client &client,
                const string &filePath,
                const string &groupId);

#endif
