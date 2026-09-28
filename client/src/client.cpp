#include "../include/client.h"
#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>
using namespace std;

Client::Client(string ip, int port){
    trackerIP = ip;
    trackerPort = port;
    sock = -1;
}

bool Client::connectToTracker(bool quiet){
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0){
        perror("[CLIENT] Socket creation failed");
        return false;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(trackerPort);

    if(inet_pton(AF_INET, trackerIP.c_str(), &serverAddr.sin_addr) <= 0){
        perror("[CLIENT] Invalid address/Address not supported");
        close(sock);
        sock = -1;
        return false;
    }

    if(connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0){
        perror("[CLIENT] Connection failed");
        close(sock);
        sock = -1;
        return false;
    }

    if(!quiet){
        cout<<"[CLIENT] Connected to tracker at "<<trackerIP<<":"<<trackerPort<<"\n";
    }
    return true;
}


void Client::disconnect(){
    if(sock >= 0){
        close(sock);
        sock = -1;
        cout<<"[CLIENT] Disconnected from tracker\n";
    }
}

bool Client::isConnected() const{
    return sock >= 0;
}

bool Client::sendMessage(const string &msg){
    if(sock < 0) return false;
    string data = msg + "\n";
    const char *ptr = data.c_str();
    size_t remaining = data.size();
    while(remaining > 0){
        ssize_t sent = send(sock, ptr, remaining, 0);
        if(sent < 0){
            if(errno == EINTR) continue;
            perror("[CLIENT] Send failed");
            return false;
        }
        ptr += sent;
        remaining -= sent;
    }
    return true;
}

string Client::receiveMessage() {
    if (sock < 0) return "";

    while (true) {
        size_t pos = recvBuf.find('\n');
        if (pos != string::npos) {

            string msg = recvBuf.substr(0, recvBuf.size());
            recvBuf.clear();

            while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) {
                msg.pop_back();
            }
            return msg;
        }

        char buffer[4096];
        ssize_t n = recv(sock, buffer, sizeof(buffer), 0);
        if (n > 0) {
            recvBuf.append(buffer, (size_t)n);
            continue;
        } else if (n == 0) {

            if (!recvBuf.empty()) {
                string msg = recvBuf;
                recvBuf.clear();
                while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r'))
                    msg.pop_back();
                return msg;
            }
            return "";
        } else {
            if (errno == EINTR) continue;
            perror("[CLIENT] Recv failed");
            return "";
        }
    }
}
