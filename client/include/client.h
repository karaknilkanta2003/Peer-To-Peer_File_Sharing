#ifndef CLIENT_H
#define CLIENT_H

#include <string>
#include <mutex>
using namespace std;

// Held for every request/response exchange on the tracker socket, so the
// background auto-share after a download never interleaves with a command.
extern mutex trackerIOMutex;

class Client {
private:
    int sock;
    string trackerIP;
    int trackerPort;
    string recvBuf;
    string recvBuffer;
    string userId;

    string selfIP;
    int selfPort;

public:

    Client(string ip, int port);

    bool connectToTracker(bool quiet = false);
    void disconnect();

    bool isConnected() const;

    bool sendMessage(const string &msg);
    string receiveMessage();

    void setSelfAddress(const string &ip, int port) {
        selfIP = ip;
        selfPort = port;
    }

    string getIP() const { return selfIP; }
    int getPort() const { return selfPort; }

    void setUserId(const string &id) { userId = id; }
    string getUserId() const { return userId; }
};

#endif
