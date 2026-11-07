#include "../include/client.h"
#include "../include/commands.h"
#include "../include/downloader.h"
#include "../include/uploader.h"
#include "../include/uploader_server.h"
#include "../include/seeds.h"
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>
#include <mutex>
#include <fcntl.h>
#include <cstring>
#include <cstdlib>
#include <csignal>

using namespace std;

// What the next tracker reply belongs to (set by handleCommand).
string lastDestPath;
string pendingLoginUser;
string pendingLoginPass;
string pendingLeaveGroup;
string pendingStopGroup;
string pendingStopFile;
PendingUpload pendingUpload;

// Credentials of the logged-in user, used to log back in after failover.
static string sessionUser;
static string sessionPass;

extern mutex seedingFilesMutex;
extern map<string, map<string, SeedInfo>> userSeeds;

static vector<pair<string, int>> readTrackerInfo(const char *infoFile) {
    vector<pair<string, int>> trackers;
    int fd = open(infoFile, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return trackers;
    }

    char buffer[1024];
    ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);
    if (n < 0) {
        perror("read");
        return trackers;
    }
    buffer[n] = '\0';

    // Accept both "IP PORT" and "IP:PORT".
    for (ssize_t i = 0; i < n; ++i) {
        if (buffer[i] == ':') buffer[i] = ' ';
    }

    char *line = strtok(buffer, "\n");
    while (line != nullptr) {
        char tIP[64];
        int tPort;
        if (sscanf(line, "%63s %d", tIP, &tPort) == 2) {
            trackers.push_back({string(tIP), tPort});
        }
        line = strtok(nullptr, "\n");
    }
    return trackers;
}

// Connects `client` to whichever tracker reports PRIMARY. Retries a few times
// because after a failover the backup needs a moment to start listening.
static bool connectToPrimary(const vector<pair<string, int>> &trackers, Client &client,
                             int attempts) {
    for (int attempt = 0; attempt < attempts; ++attempt) {
        for (auto &t : trackers) {
            Client testClient(t.first, t.second);
            if (!testClient.connectToTracker(true)) continue;

            testClient.sendMessage("WHOAREYOU");
            string resp = testClient.receiveMessage();

            if (resp.find("ROLE PRIMARY") != string::npos) {
                cout << "[CLIENT] Connected to PRIMARY tracker at "
                     << t.first << ":" << t.second << "\n";
                string ip = client.getIP();
                int port = client.getPort();
                string user = client.getUserId();
                client = testClient;
                client.setSelfAddress(ip, port);
                client.setUserId(user);
                return true;
            }
            testClient.disconnect();
        }
        if (attempt + 1 < attempts) sleep(1);
    }
    return false;
}

// Called after the tracker connection dropped. Reconnects and, if a user was
// logged in, logs them in again on the new primary.
static bool recoverConnection(const vector<pair<string, int>> &trackers, Client &client) {
    cerr << "[CLIENT] Lost connection to tracker, looking for the primary...\n";
    client.disconnect();
    if (!connectToPrimary(trackers, client, 10)) {
        cerr << "[CLIENT] No tracker is reachable.\n";
        return false;
    }
    if (!sessionUser.empty() && client.getUserId() == sessionUser) {
        client.sendMessage("login " + sessionUser + " " + sessionPass + " " +
                           client.getIP() + ":" + to_string(client.getPort()));
        string resp = client.receiveMessage();
        if (resp.rfind("OK Logged in as ", 0) == 0) {
            cout << "[CLIENT] Session restored for " << sessionUser << "\n";
        } else {
            cerr << "[CLIENT] Could not restore session: " << resp << "\n";
            stopUploaderServer();
            client.setUserId("");
            sessionUser.clear();
            sessionPass.clear();
        }
    }
    cerr << "[CLIENT] Reconnected. Please re-run your last command.\n";
    return true;
}

static void clearPending() {
    pendingLeaveGroup.clear();
    pendingStopGroup.clear();
    pendingStopFile.clear();
    pendingUpload = PendingUpload();
}

// Applies local side effects of a confirmed (or refused) command.
static void handleResponse(Client &client, const string &response) {
    bool ok = response.rfind("OK", 0) == 0;

    // Login result: on failure do not keep a seeder running for this user.
    if (!pendingLoginUser.empty()) {
        if (response.rfind("OK Logged in as ", 0) == 0) {
            string userId = pendingLoginUser;
            sessionUser = userId;
            sessionPass = pendingLoginPass;
            cout << "[CLIENT] Logged in as " << userId << endl;

            lock_guard<mutex> lock(seedingFilesMutex);
            userSeeds[userId] = loadSeeds(userId);
            cout << "[SEEDER] Restored " << userSeeds[userId].size()
                 << " shared file(s) for " << userId << endl;
            for (auto &p : userSeeds[userId]) {
                cout << "   - " << p.first << " (group=" << p.second.groupId
                     << ", path=" << p.second.path << ")" << endl;
            }
        } else {
            cout << "[TRACKER RESPONSE] " << response << "\n";
            stopUploaderServer();
            client.setUserId("");
        }
        pendingLoginUser.clear();
        pendingLoginPass.clear();
        return;
    }

    if (response.rfind("OK DOWNLOAD", 0) == 0) {
        DownloadMetadata meta = parseDownloadResponse(response);
        startDownload(client, meta, lastDestPath);
        return;
    }

    cout << "[TRACKER RESPONSE] " << response << "\n";

    if (response.rfind("OK Logged out", 0) == 0) {
        stopUploaderServer();
        client.setUserId("");
        sessionUser.clear();
        sessionPass.clear();
        cout << "[CLIENT] Local uploader server stopped.\n";
        return;
    }

    if (pendingUpload.active) {
        if (ok) commitUpload(pendingUpload);
        return;
    }

    if (!pendingLeaveGroup.empty() && ok) {
        string userId = client.getUserId();
        map<string, SeedInfo> remaining;
        {
            lock_guard<mutex> lock(seedingFilesMutex);
            auto &seeds = userSeeds[userId];
            for (auto it = seeds.begin(); it != seeds.end();) {
                if (it->second.groupId == pendingLeaveGroup) it = seeds.erase(it);
                else ++it;
            }
            remaining = seeds;
        }
        saveSeeds(userId, remaining);
        return;
    }

    if (!pendingStopFile.empty() && ok) {
        string userId = client.getUserId();
        map<string, SeedInfo> remaining;
        {
            lock_guard<mutex> lock(seedingFilesMutex);
            auto &seeds = userSeeds[userId];
            auto it = seeds.find(pendingStopFile);
            if (it != seeds.end() && it->second.groupId == pendingStopGroup) seeds.erase(it);
            remaining = seeds;
        }
        saveSeeds(userId, remaining);
        cout << "[CLIENT] Stopped sharing " << pendingStopFile << " in group "
             << pendingStopGroup << endl;
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " <IP:PORT> <tracker_info.txt>\n";
        return 1;
    }

    // A peer or tracker that disconnects mid-send must not kill the client.
    signal(SIGPIPE, SIG_IGN);

    string ipPort = argv[1];
    size_t colonPos = ipPort.find(':');
    if (colonPos == string::npos) {
        cerr << "Error: invalid format. Use <IP:PORT>\n";
        return 1;
    }
    string clientIP = ipPort.substr(0, colonPos);
    int clientPort = atoi(ipPort.substr(colonPos + 1).c_str());
    if (clientPort <= 0 || clientPort > 65535) {
        cerr << "Error: invalid port in " << ipPort << "\n";
        return 1;
    }

    cout << "[CLIENT] My listening IP: " << clientIP
         << ", Port: " << clientPort << "\n";

    vector<pair<string, int>> trackers = readTrackerInfo(argv[2]);
    if (trackers.empty()) {
        cerr << "[CLIENT] No trackers found in " << argv[2] << "\n";
        return 1;
    }

    Client client("", 0);
    client.setSelfAddress(clientIP, clientPort);
    if (!connectToPrimary(trackers, client, 3)) {
        cerr << "[CLIENT] Could not connect to any PRIMARY tracker\n";
        return 1;
    }

    cout << "[CLIENT] Type help for the list of commands.\n";

    while (true) {
        string input;
        cout << "client> " << flush;
        if (!getline(cin, input)) {
            input = "exit";   // Ctrl+D
            cout << "\n";
        }

        // Trim surrounding whitespace.
        size_t b = input.find_first_not_of(" \t\r");
        if (b == string::npos) continue;
        input = input.substr(b, input.find_last_not_of(" \t\r") - b + 1);

        lock_guard<mutex> io(trackerIOMutex);

        if (!client.isConnected() && !recoverConnection(trackers, client)) break;

        clearPending();
        int result = handleCommand(client, input);
        if (result == 1) break;
        if (result == -1 || result == 2) continue;

        string response = client.receiveMessage();
        if (response.empty()) {
            // Undo a login whose reply never arrived.
            if (!pendingLoginUser.empty()) {
                stopUploaderServer();
                client.setUserId("");
                pendingLoginUser.clear();
                pendingLoginPass.clear();
            }
            clearPending();
            if (!recoverConnection(trackers, client)) break;
            continue;
        }

        handleResponse(client, response);
    }

    // Stop the seeder thread before static destructors run; a joinable
    // std::thread at exit aborts the program.
    stopUploaderServer();
    client.disconnect();
    closeUploaderLog();
    closeDownloadLog();
    return 0;
}
