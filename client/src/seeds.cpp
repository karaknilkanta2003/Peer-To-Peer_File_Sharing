#include "../include/seeds.h"
#include <unistd.h>
#include <fcntl.h>
#include <sstream>
#include <iostream>
#include <sys/stat.h>
using namespace std;

string getSeedsFilePath(const string &userId) {
    const char *home = getenv("HOME");
    string base = (home ? home : ".");
    return base + "/.p2p_seeds_" + userId + ".txt";
}

void saveSeeds(const string &userId,
               const map<string, SeedInfo> &seedingFiles) {
    string path = getSeedsFilePath(userId);
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        perror("saveSeeds: open");
        return;
    }

    for (auto &entry : seedingFiles) {
        string line = entry.first + " " + entry.second.path + " " + entry.second.groupId + "\n";
        write(fd, line.c_str(), line.size());
    }
    close(fd);
}

map<string, SeedInfo> loadSeeds(const string &userId) {
    map<string, SeedInfo> seeds;
    string path = getSeedsFilePath(userId);
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return seeds;

    char buf[4096];
    ssize_t n;
    string content;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        content.append(buf, n);
    }
    close(fd);

    size_t pos = 0;
    while (pos < content.size()) {
        size_t end = content.find('\n', pos);
        if (end == string::npos) end = content.size();
        string line = content.substr(pos, end - pos);
        if (!line.empty()) {
            istringstream iss(line);
            string fileName, filePath, groupId;
            if (iss >> fileName >> filePath >> groupId) {
                seeds[fileName] = {filePath, groupId};
            }
        }
        pos = end + 1;
    }
    return seeds;
}
