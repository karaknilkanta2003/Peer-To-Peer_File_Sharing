#ifndef SEEDS_H
#define SEEDS_H

#include <map>
#include <string>
#include "uploader.h"
using namespace std;


string getSeedsFilePath(const string &userId);

void saveSeeds(const string &userId,
               const map<string, SeedInfo> &seedingFiles);

map<string, SeedInfo> loadSeeds(const string &userId);

#endif
