#ifndef UPLOADER_SERVER_H
#define UPLOADER_SERVER_H

#include <string>
#include <map>
#include <mutex>
#include "seeds.h"
using namespace std;

extern mutex seedingFilesMutex;
extern map<string, map<string, SeedInfo>> userSeeds;

bool startUploaderServer(const string &ip, int port, const string &userId);
void stopUploaderServer();
void closeUploaderLog();

#endif
