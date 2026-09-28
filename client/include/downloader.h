#ifndef DOWNLOADER_H
#define DOWNLOADER_H

#include <string>
#include <vector>
using namespace std;

class Client;

struct DownloadMetadata {
    string fileName;
    size_t fileSize;
    size_t numChunks;
    string fullHash;
    string groupId;
    vector<string> pieceHashes;
    vector<string> peers;
};

DownloadMetadata parseDownloadResponse(const string &response);
bool startDownload(Client &client, const DownloadMetadata &meta, const string &destPath);
void showDownloads(const string &userId);
void closeDownloadLog();

#endif
