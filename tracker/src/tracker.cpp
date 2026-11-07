#include "../include/tracker.h"
#include "../../common/include/protocol.h"  
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <arpa/inet.h>
#include <csignal> 
#include <cstring>
#include <cerrno>
using namespace std;

// Sync channel runs on client port + 100 for each tracker.
Tracker::Tracker(string ip, int port, string peerIP, int peerPort)
    : myPort(port), myIP(ip), peerIP(peerIP), peerPort(peerPort), server_fd(-1),
      sync(port + 100, peerIP, peerPort + 100, *this, systemState) {}

void Tracker::startSync() {
    sync.start();
}

Tracker::~Tracker() {
    stop();
}

void Tracker::setRole(Role r) {
    role = r;
    cout << "[TRACKER] Role set to " << (role == PRIMARY ? "PRIMARY" : "BACKUP") << "\n";
}

Tracker::Role Tracker::getRole() const { return role; }

void Tracker::start() {
    if (running.load()) return;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("[TRACKER] Socket creation failed");
        server_fd = -1;
        return;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("[TRACKER] setsockopt failed");
        close(server_fd);
        server_fd = -1;
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(myPort);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[TRACKER] Bind failed");
        close(server_fd);
        server_fd = -1;
        return;
    }

    if (listen(server_fd, 10) < 0) {
        perror("[TRACKER] Listen failed");
        close(server_fd);
        server_fd = -1;
        return;
    }

    cout << "[TRACKER] Listening for clients on port " << myPort << "\n";

    running.store(true);
    listenerThread = thread(&Tracker::listenForClients, this);

    consoleThread = thread(&Tracker::consoleLoop, this);
}

void Tracker::stop() {
    running.store(false);

    if (server_fd >= 0) {
        shutdown(server_fd, SHUT_RDWR);
        close(server_fd);
        server_fd = -1;
    }

    if (listenerThread.joinable()) {
        listenerThread.join();
    }
}

void Tracker::listenForClients() {
    while (running.load()) {
        int clientSock = accept(server_fd, nullptr, nullptr);
        if (clientSock < 0) {
            if (!running.load()) break;
            if (errno == EINTR) continue;
            perror("[TRACKER] Accept failed");
            this_thread::sleep_for(chrono::milliseconds(100));
            continue;
        }

        try {
            thread(&Tracker::handleClient, this, clientSock).detach();
        } catch (...) {
            close(clientSock);
        }
    }
}

void Tracker::handleClient(int clientSock) {
    {
        lock_guard<mutex> lock(sessionMutex);
        sessions[clientSock] = ClientSession();
    }

    {
        sockaddr_in peer{};
        socklen_t peerlen = sizeof(peer);
        if (getpeername(clientSock, (sockaddr*)&peer, &peerlen) == 0) {
            char addrbuf[INET_ADDRSTRLEN];
            if (inet_ntop(AF_INET, &peer.sin_addr, addrbuf, sizeof(addrbuf))) {
                int port = ntohs(peer.sin_port);
                string addr = string(addrbuf) + ":" + to_string(port);
                lock_guard<mutex> lock(sessionMutex);
                sessions[clientSock].peerAddr = addr;
            }
        }
    }

    char buffer[4096];
    string leftover;

    while (running.load()) {
        ssize_t bytes = recv(clientSock, buffer, sizeof(buffer) - 1, 0);
        if (bytes > 0) {
            buffer[bytes] = '\0';
            leftover += buffer;

            size_t pos;
            while ((pos = leftover.find('\n')) != string::npos) {
                string line = leftover.substr(0, pos);
                leftover.erase(0, pos + 1);

                while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
                    line.pop_back();
                }
                if (line.empty()) continue;

                cout << "[TRACKER] Received raw: [" << line << "]\n";

                string response;
                if (line == "WHOAREYOU") {
                    response = (role == PRIMARY ? "ROLE PRIMARY" : "ROLE BACKUP");
                } else {
                    response = processCommand(line, clientSock);
                }

                // Frame: body + '\n' + '\0'. The NUL marks the end of a
                // (possibly multi-line) response for the client.
                while (!response.empty() && response.back() == '\n') {
                    response.pop_back();
                }
                response.push_back('\n');
                response.push_back('\0');

                const char *ptr = response.c_str();
                size_t toSend = response.size();
                while (toSend > 0) {
                    ssize_t sent = send(clientSock, ptr, toSend, MSG_NOSIGNAL);
                    if (sent < 0) {
                        if (errno == EINTR) continue;
                        perror("[TRACKER] Send failed");
                        goto client_cleanup;
                    }
                    ptr += sent;
                    toSend -= static_cast<size_t>(sent);
                }
            }
        } else if (bytes == 0) {
            cout << "[TRACKER] Client disconnected\n";
            break;
        } else {
            if (errno == EINTR) continue;
            perror("[TRACKER] Recv failed");
            break;
        }
    }

client_cleanup:
    string userToLogout;
    {
        lock_guard<mutex> lock(sessionMutex);
        auto it = sessions.find(clientSock);
        if (it != sessions.end() && it->second.loggedIn) {
            userToLogout = it->second.userId;
            it->second.loggedIn = false;
            it->second.userId.clear();
            it->second.peerAddr.clear();
        }
    }

    if (!userToLogout.empty()) {
        string err;
        if (!systemState.logoutUser(userToLogout, err)) {
            cerr << "[TRACKER] Warning: logoutUser failed for " << userToLogout << " : " << err << "\n";
        } else {
            sync.sendUpdate(string("LOGOUT ") + userToLogout);
        }
    }

    {
        lock_guard<mutex> lock(sessionMutex);
        sessions.erase(clientSock);
    }

    close(clientSock);
}


string Tracker::processCommand(const string &cmd, int clientSock) {

    istringstream iss(cmd);
    string action;
    iss >> action;

    if (action == CMD_CREATE_USER) {
        string user, pass;
        iss >> user >> pass;
        if (user.empty() || pass.empty()) {
            return string(PROTO_ERR) + " " + to_string(ERR_INTERNAL) + " Missing arguments";
        }

        string errMsg;
        if (!systemState.createUser(user, pass, errMsg)) {
            return string(PROTO_ERR) + " " + to_string(ERR_INTERNAL) + " " + errMsg;
        }

        sync.sendUpdate(string("CREATE_USER ") + user + " " + pass);

        return string(PROTO_OK) + " User created: " + user;
    }
    else if (action == CMD_LOGIN) {
        string user, pass, peerAddr;
        iss >> user >> pass >> peerAddr;
        if (user.empty() || pass.empty() || peerAddr.empty()) {
            return string(PROTO_ERR) + " Missing arguments";
        }

        string errMsg;
        if (!systemState.loginUser(user, pass, peerAddr, errMsg)) {
            return string(PROTO_ERR) + " " + errMsg;
        }

        {
            lock_guard<mutex> lock(sessionMutex);
            sessions[clientSock].userId = user;
            sessions[clientSock].loggedIn = true;
            sessions[clientSock].peerAddr = peerAddr;
        }

        sync.sendUpdate("LOGIN " + user + " " + peerAddr);

        return string(PROTO_OK) + " Logged in as " + user;
    }

    else if (action == CMD_CREATE_GROUP) {
        string gid;
        iss >> gid;
        if (gid.empty()) {
            return string(PROTO_ERR) + " Missing group_id";
        }

        string owner;
        {
            lock_guard<mutex> lock(sessionMutex);
            if (!sessions[clientSock].loggedIn) {
                return string(PROTO_ERR) + " Must be logged in";
            }
            owner = sessions[clientSock].userId;
        }

        string errMsg;
        if (!systemState.createGroup(gid, owner, errMsg)) {
            return string(PROTO_ERR) + " " + errMsg;
        }

        sync.sendUpdate("GROUP_CREATE " + gid + " " + owner);

        return string(PROTO_OK) + " Group created: " + gid;
    }
    else if (action == CMD_LIST_GROUPS) {
        string user;
        {
            lock_guard<mutex> lock(sessionMutex);
            if (!sessions[clientSock].loggedIn) {
                return string(PROTO_ERR) + " Must be logged in";
            }
            user = sessions[clientSock].userId;
        }

        auto allGroups = systemState.listGroups();
        if (allGroups.empty()) {
            return string(PROTO_OK) + " No groups available";
        }

        string result = string(PROTO_OK) + " Groups:\n";
        for (auto &g : allGroups) {
            result += " - " + g + "\n";
        }

        return result;
    }
    else if (action == CMD_JOIN_GROUP) {
        string gid;
        iss >> gid;
        if (gid.empty()) {
            return string(PROTO_ERR) + " Missing group_id";
        }

        string user;
        {
            lock_guard<mutex> lock(sessionMutex);
            if (!sessions[clientSock].loggedIn) {
                return string(PROTO_ERR) + " Must be logged in";
            }
            user = sessions[clientSock].userId;
        }

        string errMsg;
        if (!systemState.joinGroup(gid, user, errMsg)) {
            return string(PROTO_ERR) + " " + errMsg;
        }

        sync.sendUpdate("JOIN_GROUP " + gid + " " + user);

        return string(PROTO_OK) + " Join request sent for group " + gid;
    }
    else if (action == CMD_LIST_REQUESTS) {
        string gid;
        iss >> gid;
        if (gid.empty()) {
            return string(PROTO_ERR) + " Missing group_id";
        }

        string user;
        {
            lock_guard<mutex> lock(sessionMutex);
            if (!sessions[clientSock].loggedIn) {
                return string(PROTO_ERR) + " Must be logged in";
            }
            user = sessions[clientSock].userId;
        }

        string errMsg;
        auto reqs = systemState.listRequests(gid, user, errMsg);
        if (!errMsg.empty()) {
            return string(PROTO_ERR) + " " + errMsg;
        }

        if (reqs.empty()) {
            return string(PROTO_OK) + " No pending requests for group " + gid;
        }

        string result = string(PROTO_OK) + " Pending requests for group " + gid + ":\n";
        for (auto &u : reqs) {
            result += " - " + u + "\n";
        }

        return result;
    }
    else if (action == CMD_ACCEPT_REQUEST) {
        string gid, targetUser;
        iss >> gid >> targetUser;
        if (gid.empty() || targetUser.empty()) {
            return string(PROTO_ERR) + " Missing arguments: accept_request <group_id> <user_id>";
        }

        string owner;
        {
            lock_guard<mutex> lock(sessionMutex);
            if (!sessions[clientSock].loggedIn) {
                return string(PROTO_ERR) + " Must be logged in";
            }
            owner = sessions[clientSock].userId;
        }

        string errMsg;
        if (!systemState.acceptRequest(gid, owner, targetUser, errMsg)) {
            return string(PROTO_ERR) + " " + errMsg;
        }

        sync.sendUpdate("ACCEPT_REQUEST " + gid + " " + owner + " " + targetUser);

        return string(PROTO_OK) + " User " + targetUser + " added to group " + gid;
    }
    else if (action == CMD_LEAVE_GROUP) {
        string gid;
        iss >> gid;
        if (gid.empty()) {
            return string(PROTO_ERR) + " Missing group_id";
        }

        string user;
        {
            lock_guard<mutex> lock(sessionMutex);
            if (!sessions[clientSock].loggedIn) {
                return string(PROTO_ERR) + " Must be logged in";
            }
            user = sessions[clientSock].userId;
        }

        string errMsg;
        if (!systemState.leaveGroup(gid, user, errMsg)) {
            return string(PROTO_ERR) + " " + errMsg;
        }

        sync.sendUpdate("LEAVE_GROUP " + gid + " " + user);

        return string(PROTO_OK) + " " + user + " left group " + gid;
    }
    else if (action == CMD_LOGOUT) {
        string user;
        {
            lock_guard<mutex> lock(sessionMutex);
            auto sit = sessions.find(clientSock);
            if (sit == sessions.end() || !sit->second.loggedIn) {
                return string(PROTO_ERR) + " No active login session";
            }
            user = sit->second.userId;
            sit->second.loggedIn = false;
            sit->second.userId = "";
        }

        string errMsg;
        if (!systemState.logoutUser(user, errMsg)) {
            return string(PROTO_ERR) + " " + errMsg;
        }

        sync.sendUpdate("LOGOUT " + user);

        return string(PROTO_OK) + " Logged out " + user;
    }
    else if (action == CMD_UPLOAD_FILE) {
        string gid, dummyAddr, fileName, fullHash;
        size_t fileSize = 0, numChunks = 0;

        iss >> gid >> fileName >> fileSize >> numChunks >> fullHash;
        if (gid.empty() || fileName.empty() || fileSize == 0 || numChunks == 0 || fullHash.empty()) {
            return string(PROTO_ERR) + " Missing arguments for upload_file";
        }

        vector<string> pieceHashes;
        string hash;
        for (size_t i = 0; i < numChunks; i++) {
            if (!(iss >> hash)) {
                return string(PROTO_ERR) + " Piece hashes missing";
            }
            pieceHashes.push_back(hash);
        }

        string user;
        {
            lock_guard<mutex> lock(sessionMutex);
            if (!sessions[clientSock].loggedIn) {
                return string(PROTO_ERR) + " Must be logged in";
            }
            user = sessions[clientSock].userId;
        }

        string errMsg;
        if (!systemState.checkMember(gid, user, errMsg)) {
            return string(PROTO_ERR) + " " + errMsg;
        }
        if (!systemState.uploadFile(gid, user, fileName, fileSize,
                                    numChunks, fullHash, pieceHashes, errMsg)) {
            return string(PROTO_ERR) + " " + errMsg;
        }

        ostringstream syncMsg;
        syncMsg << "UPLOAD_FILE " << gid << " " << user << " "
                << fileName << " " << fileSize << " " << numChunks << " " << fullHash;
        for (auto &h : pieceHashes) syncMsg << " " << h;
        sync.sendUpdate(syncMsg.str());

        return string(PROTO_OK) + " File " + fileName +
            " uploaded in group " + gid + " by " + user;
    }

    else if (action == CMD_LIST_FILES) {
        string gid;
        iss >> gid;
        if (gid.empty()) {
            return string(PROTO_ERR) + " Missing group_id";
        }

        string user;
        {
            lock_guard<mutex> lock(sessionMutex);
            if (!sessions[clientSock].loggedIn) {
                return string(PROTO_ERR) + " Must be logged in";
            }
            user = sessions[clientSock].userId;
        }

        string errMsg;
        if (!systemState.checkMember(gid, user, errMsg)) {
            return string(PROTO_ERR) + " " + errMsg;
        }
        auto files = systemState.listFiles(gid, errMsg);
        if (!errMsg.empty()) {
            return string(PROTO_ERR) + " " + errMsg;
        }

        if (files.empty()) {
            return string(PROTO_OK) + " No files in group " + gid;
        }

        string result = string(PROTO_OK) + " Files in group " + gid + ":\n";
        for (auto &f : files) {
            result += " - " + f + "\n";
        }
        return result;
    }
    else if (action == CMD_DOWNLOAD_FILE) {
        string gid, fileName, dest;
        iss >> gid >> fileName >> dest;
        if (gid.empty() || fileName.empty()) {
            return string(PROTO_ERR) + " Missing arguments: download_file <group_id> <file_name> <dest>";
        }

        string user;
        {
            lock_guard<mutex> lock(sessionMutex);
            if (!sessions[clientSock].loggedIn) {
                return string(PROTO_ERR) + " Must be logged in";
            }
            user = sessions[clientSock].userId;
        }

        FileMetadata meta;
        string errMsg;
        if (!systemState.checkMember(gid, user, errMsg)) {
            return string(PROTO_ERR) + " " + errMsg;
        }
        if (!systemState.getFileMetadata(gid, fileName, meta, errMsg)) {
            return string(PROTO_ERR) + " " + errMsg;
        }

        ostringstream res;
        res << PROTO_OK << " DOWNLOAD "
            << meta.fileName << " "
            << meta.fileSize << " "
            << meta.numChunks << " "
            << meta.fullHash << " "
            << meta.groupId;

        for (auto &h : meta.pieceHashes) res << " " << h;
        for (auto &p : meta.activePeers) res << " PEER " << p;

        cout << "[TRACKER] Sending DOWNLOAD response: " << res.str() << endl;

        return res.str();
    }
    else if (action == CMD_STOP_SHARE) {
        string gid, fileName;
        iss >> gid >> fileName;
        if (gid.empty() || fileName.empty()) {
            return string(PROTO_ERR) + " Missing arguments: stop_share <group_id> <file_name>";
        }

        string user;
        {
            lock_guard<mutex> lock(sessionMutex);
            if (!sessions[clientSock].loggedIn) {
                return string(PROTO_ERR) + " Must be logged in";
            }
            user = sessions[clientSock].userId;
        }

        string errMsg;
        if (!systemState.stopShare(gid, user, fileName, errMsg)) {
            return string(PROTO_ERR) + " " + errMsg;
        }

        sync.sendUpdate("STOP_SHARE " + gid + " " + fileName + " " + user);
        return string(PROTO_OK) + " " + user + " stopped sharing " + fileName + " in group " + gid;
    }


    else {
        return string(PROTO_ERR) + " " + to_string(ERR_INTERNAL) + " Unknown command: " + action;
    }
}

void Tracker::consoleLoop() {
    string line;
    while (running.load()) {
        if (!getline(cin, line)) break;

        if (line == "quit" || line == "exit") {
            cout << "[TRACKER] Quitting\n";
            exit(1);
        } else {
            cout << "[TRACKER] Unknown console command: " << line << endl;
        }
    }
}
