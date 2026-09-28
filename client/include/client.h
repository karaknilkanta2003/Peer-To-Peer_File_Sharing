#ifndef CLIENT_H
#define CLIENT_H

#include <string>
using namespace std;

class Client {
private:
    int sock;
    string trackerIP;
    int trackerPort;
    string recvBuf;
    string recvBuffer;
    string userId;
\
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
