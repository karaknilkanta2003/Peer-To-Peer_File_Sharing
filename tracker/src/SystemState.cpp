#include "../include/SystemState.h"
#include <mutex>
#include <algorithm>
using namespace std;

static mutex stateMutex;

mutex &SystemState::lock() { return stateMutex; }

bool SystemState::checkMember(const string &gid, const string &userId, string &errMsg) {
    lock_guard<mutex> lock(stateMutex);
    auto it = groups.find(gid);
    if (it == groups.end()) {
        errMsg = "Group does not exist";
        return false;
    }
    if (!it->second.members.count(userId)) {
        errMsg = "You are not a member of group " + gid;
        return false;
    }
    return true;
}

void SystemState::markLoggedIn(const string &userId, const string &peerAddr) {
    lock_guard<mutex> lock(stateMutex);
    auto it = users.find(userId);
    if (it == users.end()) return;
    it->second.loggedIn = true;
    userSessions[userId] = peerAddr;
}

bool SystemState::createUser(const string &userId, const string &password, string &errMsg) {
    lock_guard<mutex> lock(stateMutex);
    if (users.find(userId) != users.end()) {
        errMsg = "User already exists";
        return false;
    }
    users[userId] = User{userId, password, false};
    return true;
}

bool SystemState::loginUser(const string &userId, const string &password, const string &peerAddr, string &errMsg) {
    lock_guard<mutex> lock(stateMutex);
    auto it = users.find(userId);
    if (it == users.end()) {
        errMsg = "User does not exist";
        return false;
    }
    if (it->second.passwordHash != password) {
        errMsg = "Incorrect password";
        return false;
    }
    it->second.loggedIn = true;
    userSessions[userId] = peerAddr;
    return true;
}


bool SystemState::createGroup(const string &gid, const string &owner, string &errMsg) {
    lock_guard<mutex> lock(stateMutex);
    if (groups.find(gid) != groups.end()) {
        errMsg = "Group already exists";
        return false;
    }
    Group g;
    g.groupId = gid;
    g.ownerId = owner;
    g.members.insert(owner);
    g.memberOrder.push_back(owner);

    groups[gid] = g;
    return true;
}

vector<string> SystemState::listGroups() {
    lock_guard<mutex> lock(stateMutex);
    vector<string> res;
    for (auto &entry : groups) {
        res.push_back(entry.first);
    }
    return res;
}

bool SystemState::joinGroup(const string &gid, const string &userId, string &errMsg) {

    lock_guard<mutex> lock(stateMutex);
    auto it = groups.find(gid);
    if (it == groups.end()) {
        errMsg = "Group does not exist";
        return false;
    }

    Group &grp = it->second;

    if (grp.members.count(userId)) {
        errMsg = "Already a member of this group";
        return false;
    }

    if (grp.pendingRequests.count(userId)) {
        errMsg = "Join request already sent";
        return false;
    }

    grp.pendingRequests.insert(userId);
    return true;
}

vector<string> SystemState::listRequests(const string &gid, const string &owner, string &errMsg) {
    lock_guard<mutex> lock(stateMutex);
    auto it = groups.find(gid);
    if (it == groups.end()) {
        errMsg = "Group does not exist";
        return {};
    }

    Group &grp = it->second;
    if (grp.ownerId != owner) {
        errMsg = "Only the group owner can view requests";
        return {};
    }

    vector<string> requests(grp.pendingRequests.begin(), grp.pendingRequests.end());
    return requests;
}

bool SystemState::acceptRequest(const string &gid, const string &owner, const string &userId, string &errMsg) {
    lock_guard<mutex> lock(stateMutex);
    auto it = groups.find(gid);
    if (it == groups.end()) {
        errMsg = "Group does not exist";
        return false;
    }

    Group &grp = it->second;
    if (grp.ownerId != owner) {
        errMsg = "Only the group owner can accept requests";
        return false;
    }

    if (!grp.pendingRequests.count(userId)) {
        errMsg = "No such pending request from " + userId;
        return false;
    }

    grp.pendingRequests.erase(userId);
    grp.members.insert(userId);
    grp.memberOrder.push_back(userId);


    return true;
}

bool SystemState::leaveGroup(const string &gid, const string &userId, string &errMsg) {
    lock_guard<mutex> lock(stateMutex);
    auto it = groups.find(gid);
    if (it == groups.end()) {
        errMsg = "Group does not exist";
        return false;
    }

    Group &grp = it->second;

    if (!grp.members.count(userId) && grp.ownerId != userId) {
        errMsg = "You are not a member of this group";
        return false;
    }

    {
        auto fit = groupFiles.find(gid);
        if (fit != groupFiles.end()) {
            auto &files = fit->second;
            vector<string> toRemove;
            for (auto &f : files) {
                f.second.owners.erase(userId);
                if (f.second.owners.empty()) {
                    toRemove.push_back(f.first);
                }
            }
            for (auto &fname : toRemove) {
                files.erase(fname);
            }
            if (files.empty()) {
                groupFiles.erase(fit);
            }
        }
    }

    if (grp.ownerId == userId) {
        string newOwner;

        for (auto &member : grp.memberOrder) {
            if (member != userId && grp.members.count(member)) {
                newOwner = member;
                break;
            }
        }

        if (!newOwner.empty()) {
            grp.ownerId = newOwner;

            grp.members.erase(userId);
            grp.memberOrder.erase(
                remove(grp.memberOrder.begin(), grp.memberOrder.end(), userId),
                grp.memberOrder.end()
            );

            return true;
        } else {
            groups.erase(it);
            return true;
        }
    }

    grp.members.erase(userId);
    grp.memberOrder.erase(
        remove(grp.memberOrder.begin(), grp.memberOrder.end(), userId),
        grp.memberOrder.end()
    );

    return true;
}

bool SystemState::logoutUser(const string &userId, string &errMsg) {
    lock_guard<mutex> lock(stateMutex);
    auto it = users.find(userId);
    if (it == users.end()) {
        errMsg = "User does not exist";
        return false;
    }
    if (!it->second.loggedIn) {
        errMsg = "User is not logged in";
        return false;
    }

    it->second.loggedIn = false;
    userSessions.erase(userId);
    return true;
}


bool SystemState::uploadFile(const string &gid,
                             const string &userId,
                             const string &fileName,
                             size_t fileSize,
                             size_t numChunks,
                             const string &fullHash,
                             const vector<string> &pieceHashes,
                             string &errMsg) {
    lock_guard<mutex> lock(stateMutex);

    if (groups.find(gid) == groups.end()) {
        errMsg = "Group does not exist";
        return false;
    }

    auto &files = groupFiles[gid];

    auto existing = files.find(fileName);
    if (existing != files.end()) {
        // Same name must mean same content, otherwise peers would serve
        // pieces that never match the advertised hashes.
        if (existing->second.fullHash != fullHash) {
            errMsg = "A different file named " + fileName + " already exists in group " + gid;
            return false;
        }
        existing->second.owners.insert(userId);
        return true;
    }

    FileMetadata meta;
    meta.groupId    = gid;
    meta.fileName    = fileName;
    meta.fileSize    = fileSize;
    meta.numChunks   = numChunks;
    meta.fullHash    = fullHash;
    meta.pieceHashes = pieceHashes;
    meta.owners.insert(userId);

    files[fileName] = meta;
    return true;
}


vector<string> SystemState::listFiles(const string &gid, string &errMsg) {
    lock_guard<mutex> lock(stateMutex);
    auto it = groups.find(gid);
    if (it == groups.end()) {
        errMsg = "Group does not exist";
        return {};
    }

    vector<string> files;
    if (groupFiles.find(gid) != groupFiles.end()) {
        for (auto &entry : groupFiles[gid]) {
            files.push_back(entry.first);
        }
    }
    return files;
}


bool SystemState::getFileMetadata(const string &gid,
                                  const string &fileName,
                                  FileMetadata &meta,
                                  string &errMsg) {
    lock_guard<mutex> lock(stateMutex);
    if (groups.find(gid) == groups.end()) {
        errMsg = "Group does not exist";
        return false;
    }

    auto gIt = groupFiles.find(gid);
    if (gIt == groupFiles.end() || gIt->second.find(fileName) == gIt->second.end()) {
        errMsg = "File not found in group " + gid;
        return false;
    }

    meta = gIt->second[fileName];

    set<string> resolved;
    for (auto &userId : meta.owners) {
        if (userSessions.count(userId)) {
            resolved.insert(userSessions[userId]);
        }
    }
    meta.activePeers = resolved;

    return true;
}

bool SystemState::stopShare(const string &gid, const string &userId,
                            const string &fileName, string &errMsg) {
    lock_guard<mutex> lock(stateMutex);

    auto gIt = groupFiles.find(gid);
    if (gIt == groupFiles.end()) {
        errMsg = "Group " + gid + " not found";
        return false;
    }

    auto &files = gIt->second;
    auto fIt = files.find(fileName);
    if (fIt == files.end()) {
        errMsg = "File " + fileName + " not found in group " + gid;
        return false;
    }

    FileMetadata &meta = fIt->second;

    if (meta.owners.find(userId) == meta.owners.end()) {
        errMsg = "User " + userId + " is not an owner of file " + fileName;
        return false;
    }

    meta.owners.erase(userId);

    auto sit = userSessions.find(userId);
    if (sit != userSessions.end()) {
        meta.activePeers.erase(sit->second);
    }

    if (meta.owners.empty()) {
        files.erase(fIt);
        if (files.empty()) groupFiles.erase(gIt);
    }

    return true;
}
