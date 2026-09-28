#include "commands.h"
#include "client.h"
#include "uploader.h"
#include "downloader.h"
#include "uploader_server.h"
#include "../../common/include/protocol.h"
#include "../include/seeds.h"
#include <iostream>
#include <sstream>
using namespace std;

extern string lastDestPath;
extern string pendingLoginUser;

int handleCommand(Client &client, const string &input) {
    istringstream iss(input);
    string cmd;
    iss >> cmd;

    vector<string> args;
    string arg;
    while(iss >> arg) args.push_back(arg);

    if (cmd == CMD_CREATE_USER) {
        if (args.size() < 2) {
            cerr << "Usage: create_user <user> <pass>\n";
            return -1;
        }
        client.sendMessage(string(CMD_CREATE_USER) + " " + args[0] + " " + args[1]);
    }
    else if (cmd == CMD_LOGIN) {
        if (args.size() < 2) {
            cerr << "Usage: login <user> <pass>\n";
            return -1;
        }
        // A second login in the same process leaves the tracker holding a
        // session for the earlier user at this address, while we can only ever
        // serve one user from one port. Force an explicit logout instead.
        if (!client.getUserId().empty()) {
            cerr << "[CLIENT] Already logged in as " << client.getUserId()
                 << ". Run logout before logging in as another user.\n";
            return -1;
        }
        // Bring the seeder up as this user BEFORE telling the tracker we are
        // reachable here. Announcing first meant a failed/skipped seeder start
        // left the tracker advertising our address for a user we do not serve,
        // and every GET_PIECE came back "ERR File not available".
        if (!startUploaderServer(client.getIP(), client.getPort(), args[0])) {
            cerr << "[CLIENT] Failed to start uploader server for user " << args[0]
                 << "; not logging in.\n";
            return -1;
        }
        client.setUserId(args[0]);
        pendingLoginUser = args[0];
        client.sendMessage(string(CMD_LOGIN)  + " " + args[0] + " " + args[1] + " " + client.getIP() + ":" + to_string(client.getPort()));
    }
    else if (cmd == CMD_CREATE_GROUP) {
        if (args.empty()) {
            cerr << "Usage: create_group <group_id>\n";
            return -1;
        }
        client.sendMessage(string(CMD_CREATE_GROUP) + " " + args[0]);
    }
    else if (cmd == CMD_LIST_GROUPS) {
        client.sendMessage(string(CMD_LIST_GROUPS));
    }
    else if (cmd == CMD_JOIN_GROUP) {
        if (args.empty()) {
            cerr << "Usage: join_group <group_id>\n";
            return -1;
        }
        client.sendMessage(string(CMD_JOIN_GROUP) + " " + args[0]);
    }
    else if (cmd == CMD_LIST_REQUESTS) {
        if (args.empty()) {
            cerr << "Usage: list_requests <group_id>\n";
            return -1;
        }
        client.sendMessage(string(CMD_LIST_REQUESTS) + " " + args[0]);
    }
    else if (cmd == CMD_ACCEPT_REQUEST) {
        if (args.size() < 2) {
            cerr << "Usage: accept_request <group_id> <user_id>\n";
            return -1;
        }
        client.sendMessage(string(CMD_ACCEPT_REQUEST) + " " + args[0] + " " + args[1]);
    }
    else if (cmd == CMD_LEAVE_GROUP) {
        if (args.empty()) {
            cerr << "Usage: leave_group <group_id>\n";
            return -1;
        }
        string gid = args[0];
        string userId = client.getUserId();
        if (userId.empty()) {
            cerr << "[CLIENT] Not logged in.\n";
            return -1;
        }
        client.sendMessage(string(CMD_LEAVE_GROUP) + " " + args[0]);
        map<string, SeedInfo> remaining;
        bool haveSeeds = false;
        {
            lock_guard<mutex> lock(seedingFilesMutex);
            auto it = userSeeds.find(userId);
            if (it != userSeeds.end()) {
                vector<string> toErase;
                for (auto &p : it->second) {
                    if (p.second.groupId == gid) {
                        toErase.push_back(p.first);
                    }
                }
                for (auto &fname : toErase) {
                    it->second.erase(fname);
                }
                remaining = it->second;
                haveSeeds = true;
            }
        }

        // Only rewrite the seed file if this user actually has an in-memory
        // table. userSeeds[userId] would default-construct an empty map and
        // truncate the saved seeds to nothing.
        if (haveSeeds) {
            saveSeeds(userId, remaining);
        }

    }
    else if (cmd == CMD_LOGOUT) {
        client.sendMessage(string(CMD_LOGOUT));
    }
    else if (cmd == CMD_UPLOAD_FILE) {
        if (args.size() < 2) {
        cerr << "Usage: upload_file <group_id> <file_path>\n";
        return -1;
        }
        if (!uploadFile(client, args[1], args[0])) {
            cerr << "[CLIENT] Upload failed\n";
            return -1;
        }
    }
    else if (cmd == CMD_LIST_FILES) {
        if (args.empty()) {
            cerr << "Usage: list_files <group_id>\n";
            return -1;
        }
        client.sendMessage(string(CMD_LIST_FILES) + " " + args[0] + "\n");
    }
    else if (cmd == CMD_DOWNLOAD_FILE) {
        if (args.size() < 3) {
            cerr << "Usage: download_file <group_id> <file_name> <dest_path>\n";
            return -1;
        }
        lastDestPath = args[2];
        client.sendMessage(string(CMD_DOWNLOAD_FILE) + " " + args[0] + " " + args[1] + " " + args[2] + "\n");
    }
    else if (cmd == CMD_SHOW_DOWNLOADS) {
        string userId = client.getUserId();
        if (userId.empty()) {
            cerr << "[CLIENT] Not logged in.\n";
            return -1;
        }

        showDownloads(userId);
        return 2;
        
    }
    else if (cmd == CMD_STOP_SHARE) {
        if (args.size() < 2) {
            cerr << "Usage: stopshare <group_id> <file_name>\n";
            return -1;
        }

        string gid = args[0];
        string fileName = args[1];
        string userId = client.getUserId();
        if (userId.empty()) {
            cerr << "[CLIENT] Not logged in.\n";
            return -1;
        }

        client.sendMessage(string(CMD_STOP_SHARE) + " " + gid + " " + fileName + "\n");

        {
            lock_guard<mutex> lock(seedingFilesMutex);
            auto it = userSeeds.find(userId);
            if (it != userSeeds.end()) {
                it->second.erase(fileName);
            }
        }

        auto seeds = loadSeeds(userId);
        auto sit = seeds.find(fileName);
        if (sit != seeds.end()) {
            seeds.erase(sit);
        }
        saveSeeds(userId, seeds);

        cout << "[CLIENT] Stopped sharing file: " << fileName << " from group " << gid << endl;
    }


    else if (cmd == CMD_EXIT) {
        cout<<"[CLIENT] Exiting...\n";
        return 1;
    }
    else {
        cerr<<"[CLIENT] Unknown command: "<<cmd<<"\n";
        return -1;
    }

    return 0;
}
