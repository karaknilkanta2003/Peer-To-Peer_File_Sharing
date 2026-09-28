#ifndef TRACKER_H
#define TRACKER_H

#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include "SystemState.h"
#include "sync.h"
using namespace std;

struct ClientSession {
    string userId;
    bool loggedIn = false;
    string peerAddr;
};


class Tracker {
private:
    int myPort;
    string myIP;
    string peerIP;
    int peerPort;

    int server_fd;
    thread listenerThread;
    mutex stateMutex;
    atomic<bool> running{false};

    SystemState systemState;
    TrackerSync sync; 

    unordered_map<string, string> users;
    mutex sessionMutex;
    unordered_map<int, ClientSession> sessions;

    void handleClient(int clientSock);
    void listenForClients();

    thread consoleThread;
    void consoleLoop();

public:
    Tracker(string ip, int port, string peerIP, int peerPort);
    ~Tracker();

    void start();
    void stop();

    void startSync();

    string processCommand(const string &cmd, int clientSock);

    enum Role { PRIMARY, BACKUP };
    Role role = BACKUP;

    void setRole(Role r);
    Role getRole() const;

    SystemState& getSystemState() { return systemState; }
};

#endif
