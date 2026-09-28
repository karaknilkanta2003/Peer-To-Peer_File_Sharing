#ifndef COMMANDS_H
#define COMMANDS_H

#include <string>
#include <vector>
using namespace std;

class Client;
int handleCommand(Client &client, const string &input);

#endif
