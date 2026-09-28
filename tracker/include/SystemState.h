#ifndef SYSTEMSTATE_H
#define SYSTEMSTATE_H

#include <string>
#include <vector>
#include <map>
#include <set>
using namespace std;

struct User {
    string userId;
    string passwordHash;
    bool loggedIn;

    User() : loggedIn(false) {}
    User(const string& id, const string& pass, bool logged = false) : userId(id), passwordHash(pass), loggedIn(logged) {}
};


struct Group {
    string groupId;
    string ownerId;
    set<string> members;
    set<string> pendingRequests;
    vector<string> memberOrder; 
};

struct FileMetadata {
    string groupId;
    string fileName;
    size_t fileSize;
    size_t numChunks;
    string fullHash;
    vector<string> pieceHashes;
    set<string> owners;
    set<string> activePeers;
};

class SystemState {
public:
    bool createUser(const string &userId, const string &password, string &errMsg);
    bool loginUser(const string &userId, const string &password, const string &peerAddr, string &errMsg);
    bool createGroup(const string &gid, const string &owner, string &errMsg);
    vector<string> listGroups();
    bool joinGroup(const string &gid, const string &userId, string &errMsg);
    vector<string> listRequests(const string &gid, const string &owner, string &errMsg);
    bool acceptRequest(const string &gid, const string &owner, const string &userId, string &errMsg);
    bool leaveGroup(const string &gid, const string &userId, string &errMsg);
    bool logoutUser(const string &userId, string &errMsg);
    bool uploadFile(const string &gid, const string &userId, const string &fileName, size_t fileSize, size_t numChunks, const string &fullHash, const vector<string> &pieceHashes, string &errMsg);
    vector<string> listFiles(const string &gid, string &errMsg);
    bool getFileMetadata(const string &gid, const string &fileName, FileMetadata &meta, string &errMsg);
    bool stopShare(const string &gid, const string &userId, const string &fileName, string &errMsg);

    map<string, User> users;
    map<string, Group> groups;
    map<string, map<string, FileMetadata>> groupFiles;
    
    map<string, string> userSessions;
};

#endif
