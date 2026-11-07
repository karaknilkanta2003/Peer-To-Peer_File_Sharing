#include "../include/sync.h"
#include "../include/SystemState.h"
#include "../include/tracker.h"
#include <iostream>
#include <unistd.h>
#include <sstream>
#include <arpa/inet.h>
#include <cstring>
#include <algorithm>
#include <cerrno>
using namespace std;

TrackerSync::TrackerSync(int mySyncPort, string peerIP, int peerSyncPort, Tracker &t, SystemState &s) : mySyncPort(mySyncPort), peerIP(peerIP), peerSyncPort(peerSyncPort), tracker(t), systemState(s) {
    listenSock = -1;
    connSock = -1;
}

void TrackerSync::start() {
    connectToPeer();
    if (connSock == -1) {
        tracker.setRole(Tracker::PRIMARY);
        cout << "[SYNC] Acting as PRIMARY\n";
        setupServerSocket();
        thread(&TrackerSync::acceptBackupConnection, this).detach();
    } else {
        tracker.setRole(Tracker::BACKUP);
        cout << "[SYNC] Acting as BACKUP\n";
        syncFromPrimary();
        thread(&TrackerSync::handlePeerUpdates, this).detach();
    }
}

void TrackerSync::connectToPeer() {
    connSock = socket(AF_INET, SOCK_STREAM, 0);
    if (connSock < 0) {
        perror("socket");
        connSock = -1;
        return;
    }
    struct sockaddr_in peerAddr;
    peerAddr.sin_family = AF_INET;
    peerAddr.sin_port = htons(peerSyncPort);
    inet_pton(AF_INET, peerIP.c_str(), &peerAddr.sin_addr);
    if (connect(connSock, (struct sockaddr*)&peerAddr, sizeof(peerAddr)) < 0) {
        close(connSock.load());
        connSock = -1;
    }
}

void TrackerSync::setupServerSocket() {
    listenSock = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSock < 0) {
        perror("socket");
        exit(1);
    }
    int opt = 1;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(mySyncPort);
    if (bind(listenSock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listenSock);
        exit(1);
    }
    if (listen(listenSock, 1) < 0) {
        perror("listen");
        close(listenSock);
        exit(1);
    }
    cout<<"[SYNC] Primary listening on port "<<mySyncPort<<"\n";
}

void TrackerSync::acceptBackupConnection() {
    struct sockaddr_in clientAddr;
    socklen_t addrLen = sizeof(clientAddr);
    cout<<"[SYNC] Waiting for backup to connect...\n";
    connSock = accept(listenSock, (struct sockaddr*)&clientAddr, &addrLen);
    if (connSock < 0) {
        perror("accept");
        this_thread::sleep_for(chrono::seconds(1));
        thread(&TrackerSync::acceptBackupConnection, this).detach();
        return;
    }
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));
    cout<<"[SYNC] Backup connected from "<<ip<<":"<<ntohs(clientAddr.sin_port)<<"\n";
    thread(&TrackerSync::handlePeerUpdates, this).detach();
}

void TrackerSync::sendUpdate(const string &update) {
    if (tracker.getRole() != Tracker::PRIMARY) return;
    {
        lock_guard<mutex> lock(logMutex);
        updateLog.push_back(update);
    }
    if (connSock != -1) {
        sendMessage("UPDATE " + update);
    }
}

string TrackerSync::serializeSystemState() {
    lock_guard<mutex> guard(SystemState::lock());
    string out;

    for (auto &p : systemState.users) {
        const User &u = p.second;
        out += "USER "+u.userId+" "+u.passwordHash+" " + (u.loggedIn ? "1" : "0") + "\n";
    }

    for (auto &p : systemState.userSessions) {
        out += "SESSION " + p.first + " " + p.second + "\n";
    }

    for (auto &g : systemState.groups) {
        const Group &grp = g.second;
        out += "GROUP " + grp.groupId + " " + grp.ownerId + "\n";
        for (auto &m : grp.members) {
            out += "MEMBER " + grp.groupId + " " + m + "\n";
        }
        for (auto &r : grp.pendingRequests) {
            out += "REQUEST " + grp.groupId + " " + r + "\n";
        }
        for (auto &ord : grp.memberOrder) {
            out += "ORDER " + grp.groupId + " " + ord + "\n";
        }
    }

    for (auto &g : systemState.groupFiles) {
        const string &gid = g.first;
        for (auto &f : g.second) {
            const FileMetadata &fm = f.second;
            out += "FILE " + gid + " " + fm.fileName + " "
                + to_string(fm.fileSize) + " "
                + to_string(fm.numChunks) + " "
                + fm.fullHash + "\n";

            for (auto &ph : fm.pieceHashes) {
                out += "PIECE " + gid + " " + fm.fileName + " " + ph + "\n";
            }

            for (auto &owner : fm.owners) {
                out += "OWNER " + gid + " " + fm.fileName + " " + owner + "\n";
            }

            for (auto &peer : fm.activePeers) {
                out += "ACTIVE " + gid + " " + fm.fileName + " " + peer + "\n";
            }
        }
    }

    if (out.empty()) {
        out = "EMPTY\n";
    }

    return out;
}


void TrackerSync::deserializeSystemState(const string &data) {
    lock_guard<mutex> guard(SystemState::lock());
    systemState = SystemState();
    istringstream iss(data);
    string line;
    while (getline(iss, line)) {
        if (line.empty()) continue;

        istringstream ls(line);
        string type;
        ls >> type;

        if (type == "USER") {
            string userId, pass;
            string logged;
            ls >> userId >> pass >> logged;
            User u;
            u.userId = userId;
            u.passwordHash = pass;
            u.loggedIn = (logged == "1");
            systemState.users[userId] = u;
        }
        else if (type == "SESSION") {
            string userId, addr;
            ls >> userId >> addr;
            systemState.userSessions[userId] = addr;
        }
        else if (type == "GROUP") {
            string gid, owner;
            ls >> gid >> owner;
            Group g;
            g.groupId = gid;
            g.ownerId = owner;
            systemState.groups[gid] = g;
        } 
        else if (type == "MEMBER") {
            string gid, member;
            ls >> gid >> member;
            systemState.groups[gid].members.insert(member);
        } 
        else if (type == "REQUEST") {
            string gid, user;
            ls >> gid >> user;
            systemState.groups[gid].pendingRequests.insert(user);
        } 
        else if (type == "FILE") {
            string gid, fname, fullHash;
            size_t fsize, numChunks;
            ls >> gid >> fname >> fsize >> numChunks >> fullHash;

            FileMetadata fm;
            fm.groupId = gid;
            fm.fileName = fname;
            fm.fileSize = fsize;
            fm.numChunks = numChunks;
            fm.fullHash = fullHash;

            systemState.groupFiles[gid][fname] = fm;
        }
        else if (type == "PIECE") {
            string gid, fname, hash;
            ls >> gid >> fname >> hash;
            systemState.groupFiles[gid][fname].pieceHashes.push_back(hash);
        }
        else if (type == "OWNER") {
            string gid, fname, owner;
            ls >> gid >> fname >> owner;
            systemState.groupFiles[gid][fname].owners.insert(owner);
        }
        else if (type == "ACTIVE") {
            string gid, fname, peer;
            ls >> gid >> fname >> peer;
            systemState.groupFiles[gid][fname].activePeers.insert(peer);
        }
        else if (type == "ORDER") {
            string gid, user;
            ls >> gid >> user;
            systemState.groups[gid].memberOrder.push_back(user);
        }

    }
}

void TrackerSync::applyUpdate(const string &update) {
    char type[32] = {0}, arg1[128] = {0}, arg2[128] = {0}, arg3[128] = {0};
    if (sscanf(update.c_str(), "%31s %127s %127s %127s", type, arg1, arg2, arg3) < 2) {
        return;
    }

    string t(type);

    if (t == "CREATE_USER") {
        string errMsg;
        systemState.createUser(arg1, arg2, errMsg);
    }
    else if (t == "LOGIN") {
        systemState.markLoggedIn(arg1, arg2);
    }
    else if (t == "GROUP_CREATE") {
        string errMsg;
        systemState.createGroup(arg1, arg2, errMsg);
    }
    else if (t == "JOIN_GROUP") {
        string errMsg;
        systemState.joinGroup(arg1, arg2, errMsg);
    }
    else if (t == "ACCEPT_REQUEST") {
        // ACCEPT_REQUEST <gid> <owner> <newMember>
        string errMsg;
        systemState.acceptRequest(arg1, arg2, arg3, errMsg);
    }
    else if (t == "LEAVE_GROUP") {
        string errMsg;
        systemState.leaveGroup(arg1, arg2, errMsg);
    }
    else if (t == "LOGOUT") {
        string errMsg;
        systemState.logoutUser(arg1, errMsg);
    }
    else if (t == "UPLOAD_FILE") {
        istringstream ls(update);
        string cmd, gid, userId, fileName, fullHash;
        size_t fileSize, numChunks;
        ls >> cmd >> gid >> userId >> fileName >> fileSize >> numChunks >> fullHash;

        vector<string> pieceHashes;
        string hash;
        for (size_t i = 0; i < numChunks; i++) {
            if (!(ls >> hash)) break;
            pieceHashes.push_back(hash);
        }

        string errMsg;
        systemState.uploadFile(gid, userId, fileName, fileSize, numChunks, fullHash, pieceHashes, errMsg);
    }

    else if (t == "STOP_SHARE") {
        istringstream ls(update);
        string cmd, gid, fname, user;
        ls >> cmd >> gid >> fname >> user;

        string errMsg;
        systemState.stopShare(gid, user, fname, errMsg);
    }
}

void TrackerSync::handlePeerUpdates() {
    cout << "[SYNC] handlePeerUpdates loop started\n";
    while (true) {
        string msg = recvMessage();
        cout << "[SYNC] Received raw: [" << msg << "]\n";

        if (msg == "__DISCONNECT__") {
            if (tracker.getRole() == Tracker::PRIMARY) {
                cout << "[SYNC] Backup disconnected\n";
                {
                    lock_guard<mutex> lock(sendMutex);
                    if (connSock != -1) {
                        close(connSock.load());
                        connSock = -1;
                    }
                }
                thread(&TrackerSync::acceptBackupConnection, this).detach();
                return;
            } else {
                cout << "[SYNC] Peer disconnected, promoting to PRIMARY\n";
                promoteToPrimary();
                return;
            }
        }

        if (msg.empty()) {
            cout << "[SYNC] WARNING: empty message received\n";
            continue;
        }

        if (msg == "SYNC_REQUEST" && tracker.getRole() == Tracker::PRIMARY) {
            cout << "[SYNC] Received SYNC_REQUEST from backup\n";
            // Hold sendMutex across snapshot+send so no UPDATE for a change
            // made after the snapshot can overtake it.
            lock_guard<mutex> lock(sendMutex);
            string snapshot = serializeSystemState();

            for (char &c : snapshot) {
                if (c == '\n') c = ';';
            }
            sendRaw("SYNC_RESPONSE " + snapshot);
            cout << "[SYNC] Sent full state to backup\n";
        } 
        else if (msg.rfind("SYNC_RESPONSE ", 0) == 0 && tracker.getRole() == Tracker::BACKUP) {
            string stateStr = msg.substr(14);
            for (char &c : stateStr) {
                if (c == ';') c = '\n';
            }
            deserializeSystemState(stateStr);
            cout << "[SYNC] Loaded state from primary\n";
        } 
        else if (msg.rfind("UPDATE ", 0) == 0) {
            string update = msg.substr(7);
            applyUpdate(update);
            cout << "[SYNC] Applied update: " << update << "\n";
        } 
        else {
            cout << "[SYNC] Unknown message: " << msg << "\n";
        }
    }
}

void TrackerSync::syncFromPrimary() {
    sendMessage("SYNC_REQUEST");
    cout << "[SYNC] Sent SYNC_REQUEST to Primary\n";
}

void TrackerSync::promoteToPrimary() {
    if (tracker.getRole() == Tracker::PRIMARY) return;
    tracker.setRole(Tracker::PRIMARY);
    cout << "[SYNC] Promoted to PRIMARY (peer died)\n";
    {
        lock_guard<mutex> lock(sendMutex);
        if (connSock != -1) {
            close(connSock.load());
            connSock = -1;
        }
    }

    tracker.start();
    setupServerSocket();
    thread(&TrackerSync::acceptBackupConnection, this).detach();
}

void TrackerSync::replayLog() {
    if (tracker.getRole() != Tracker::PRIMARY || connSock == -1) return;
    lock_guard<mutex> lock(logMutex);
    lock_guard<mutex> slock(sendMutex);
    for (const auto &entry : updateLog) {
        sendRaw("UPDATE " + entry);
    }
    cout << "[SYNC] Replayed " << updateLog.size() << " updates to backup\n";
}

void TrackerSync::sendMessage(const string &msg) {
    lock_guard<mutex> lock(sendMutex);
    sendRaw(msg);
}

void TrackerSync::sendRaw(const string &msg) {
    int sock = connSock.load();
    if (sock == -1) return;
    string data = msg;
    if (data.empty() || data.back() != '\n') {
        data.push_back('\n');
    }
    // Loop: large UPLOAD_FILE updates may not go out in one send().
    const char *ptr = data.c_str();
    size_t left = data.size();
    while (left > 0) {
        ssize_t n = send(sock, ptr, left, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("[SYNC] send failed");
            return;
        }
        ptr += n;
        left -= (size_t)n;
    }
}

string TrackerSync::recvMessage() {
    if (connSock == -1) return "__DISCONNECT__";

    string line;
    char ch;
    while (true) {
        int n = recv(connSock, &ch, 1, 0);
        if (n == 1) {
            if (ch == '\n') break;
            line.push_back(ch);
        } else if (n == 0) {
            cout << "[SYNC] recvMessage(): peer closed connection\n";
            return "__DISCONNECT__";
        } else {
            if (errno == EINTR) continue;
            perror("[SYNC] recvMessage error");
            return "__DISCONNECT__";
        }
    }

    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }

    return line;
}
