#ifndef SYNC_H
#define SYNC_H

#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include "SystemState.h"
using namespace std;

class Tracker;

enum TrackerRole { PRIMARY, BACKUP };

class TrackerSync {
private:
    int listenSock;
    int connSock;

    int mySyncPort;
    string peerIP;
    int peerSyncPort;

    mutex logMutex;
    vector<string> updateLog;

    Tracker &tracker;

    SystemState &systemState;

    atomic<bool> hasBackup{false};

    void sendMessage(const string &msg);
    string recvMessage();

    string serializeSystemState();
    void deserializeSystemState(const string &data);
    void applyUpdate(const string &update);

public:
    TrackerSync(int mySyncPort, string peerIP, int peerSyncPort, Tracker &t, SystemState &s);

    void start();
    void connectToPeer();
    void sendUpdate(const string &update);
    void handlePeerUpdates();
    void syncFromPrimary();
    void promoteToPrimary();
    void replayLog();

    void setupServerSocket();
    void acceptBackupConnection();

    
};

#endif
