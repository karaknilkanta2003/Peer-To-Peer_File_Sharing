#include "commands.h"
#include "client.h"
#include "uploader.h"
#include "downloader.h"
#include "uploader_server.h"
#include "../../common/include/protocol.h"
#include "../include/seeds.h"
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <climits>
#include <cstdlib>
using namespace std;

// State shared with main.cpp: what the next tracker reply belongs to.
extern string lastDestPath;
extern string pendingLoginUser;
extern string pendingLoginPass;
extern string pendingLeaveGroup;
extern string pendingStopGroup;
extern string pendingStopFile;
extern PendingUpload pendingUpload;

static bool requireLogin(Client &client) {
    if (client.getUserId().empty()) {
        cerr << "[CLIENT] Not logged in. Use: login <user_id> <password>\n";
        return false;
    }
    return true;
}

static string absolutePath(const string &p) {
    char resolved[PATH_MAX];
    if (realpath(p.c_str(), resolved)) return resolved;
    return p;
}

static void printHelp() {
    cout <<
        "Commands:\n"
        "  create_user <user_id> <password>\n"
        "  login <user_id> <password>\n"
        "  logout\n"
        "  create_group <group_id>\n"
        "  join_group <group_id>\n"
        "  leave_group <group_id>\n"
        "  list_groups\n"
        "  list_requests <group_id>\n"
        "  accept_request <group_id> <user_id>\n"
        "  upload_file <group_id> <file_path>\n"
        "  list_files <group_id>\n"
        "  download_file <group_id> <file_name> <destination_dir_or_file>\n"
        "  show_downloads\n"
        "  stop_share <group_id> <file_name>\n"
        "  help\n"
        "  exit\n";
}

// Return values: 0 = a request was sent, read the tracker reply;
// -1 = error, nothing sent; 1 = exit; 2 = handled locally, nothing sent.
int handleCommand(Client &client, const string &input) {
    istringstream iss(input);
    string cmd;
    iss >> cmd;

    vector<string> args;
    string arg;
    while (iss >> arg) args.push_back(arg);

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
        // One client process serves one user from one port.
        if (!client.getUserId().empty()) {
            cerr << "[CLIENT] Already logged in as " << client.getUserId()
                 << ". Run logout before logging in as another user.\n";
            return -1;
        }
        // Start the seeder before announcing our address to the tracker.
        if (!startUploaderServer(client.getIP(), client.getPort(), args[0])) {
            cerr << "[CLIENT] Failed to start uploader server on " << client.getIP() << ":"
                 << client.getPort() << " (port in use?); not logging in.\n";
            return -1;
        }
        client.setUserId(args[0]);
        pendingLoginUser = args[0];
        pendingLoginPass = args[1];
        client.sendMessage(string(CMD_LOGIN) + " " + args[0] + " " + args[1] + " " +
                           client.getIP() + ":" + to_string(client.getPort()));
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
        if (!requireLogin(client)) return -1;
        // Local seeds for this group are dropped once the tracker says OK.
        pendingLeaveGroup = args[0];
        client.sendMessage(string(CMD_LEAVE_GROUP) + " " + args[0]);
    }
    else if (cmd == CMD_LOGOUT) {
        client.sendMessage(string(CMD_LOGOUT));
    }
    else if (cmd == CMD_UPLOAD_FILE) {
        if (args.size() < 2) {
            cerr << "Usage: upload_file <group_id> <file_path>\n";
            return -1;
        }
        if (!requireLogin(client)) return -1;
        if (!uploadFile(client, args[1], args[0], pendingUpload)) {
            cerr << "[CLIENT] Upload failed\n";
            return -1;
        }
    }
    else if (cmd == CMD_LIST_FILES) {
        if (args.empty()) {
            cerr << "Usage: list_files <group_id>\n";
            return -1;
        }
        client.sendMessage(string(CMD_LIST_FILES) + " " + args[0]);
    }
    else if (cmd == CMD_DOWNLOAD_FILE) {
        if (args.size() < 3) {
            cerr << "Usage: download_file <group_id> <file_name> <destination_dir_or_file>\n";
            return -1;
        }
        if (!requireLogin(client)) return -1;

        // The destination may be a directory (file keeps its name) or a full
        // file path.
        string dest = args[2];
        struct stat st;
        bool isDir = (stat(dest.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
        if (isDir || dest.back() == '/') {
            if (dest.back() != '/') dest += '/';
            dest += args[1];
        }

        // Never truncate a file we are currently seeding.
        string absDest = absolutePath(dest);
        {
            lock_guard<mutex> lock(seedingFilesMutex);
            auto it = userSeeds.find(client.getUserId());
            if (it != userSeeds.end()) {
                for (auto &p : it->second) {
                    if (absolutePath(p.second.path) == absDest) {
                        cerr << "[CLIENT] " << dest << " is a file you are sharing; "
                             << "choose another destination.\n";
                        return -1;
                    }
                }
            }
        }

        lastDestPath = dest;
        client.sendMessage(string(CMD_DOWNLOAD_FILE) + " " + args[0] + " " + args[1]);
    }
    else if (cmd == CMD_SHOW_DOWNLOADS) {
        if (!requireLogin(client)) return -1;
        showDownloads(client.getUserId());
        return 2;
    }
    else if (cmd == CMD_STOP_SHARE) {
        if (args.size() < 2) {
            cerr << "Usage: stop_share <group_id> <file_name>\n";
            return -1;
        }
        if (!requireLogin(client)) return -1;
        // Local seed is removed once the tracker confirms.
        pendingStopGroup = args[0];
        pendingStopFile = args[1];
        client.sendMessage(string(CMD_STOP_SHARE) + " " + args[0] + " " + args[1]);
    }
    else if (cmd == "help") {
        printHelp();
        return 2;
    }
    else if (cmd == CMD_EXIT || cmd == "quit") {
        cout << "[CLIENT] Exiting...\n";
        return 1;
    }
    else {
        cerr << "[CLIENT] Unknown command: " << cmd << " (type help)\n";
        return -1;
    }

    return 0;
}
