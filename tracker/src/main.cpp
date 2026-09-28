#include "../include/tracker.h"
#include "../include/sync.h"
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdlib>
using namespace std;

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <tracker_info.txt> <tracker_no>\n";
        return 1;
    }

    const char* infoFile = argv[1];
    int trackerNo = stoi(argv[2]);

    int fd = open(infoFile, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    char buffer[1024];
    ssize_t n = read(fd, buffer, sizeof(buffer)-1);
    if (n < 0) {
        perror("read");
        close(fd);
        return 1;
    }
    buffer[n] = '\0';
    close(fd);

    vector<pair<string,int>> trackers;
    char* line = strtok(buffer, "\n");
    while (line != nullptr) {
        char ip[64];
        int port;
        if (sscanf(line, "%63s %d", ip, &port) == 2) {
            trackers.push_back({string(ip), port});
        }
        line = strtok(nullptr, "\n");
    }

    if (trackerNo < 1 || trackerNo > (int)trackers.size()) {
        cerr << "Error: invalid tracker_no\n";
        return 1;
    }

    string myIP = trackers[trackerNo-1].first;
    int myPort = trackers[trackerNo-1].second;

    int peerNo = (trackerNo == 1 ? 2 : 1);
    string peerIP = trackers[peerNo-1].first;
    int peerPort       = trackers[peerNo-1].second;

    cout << "[MAIN] Tracker " << trackerNo << " running at " << myIP << ":" << myPort << ", peer at " << peerIP << ":" << peerPort << "\n";

    int syncPort = myPort + 1000;
    int peerSyncPort = peerPort + 1000;

    Tracker tracker(myIP, myPort, peerIP, peerPort);

    tracker.startSync();

    if (tracker.getRole() == Tracker::PRIMARY) {
        tracker.start();
    }

    while (true) {
        sleep(10);
    }

    return 0;
}
