#ifndef ZDOOM_VITA_LAUNCHER_H
#define ZDOOM_VITA_LAUNCHER_H
#include <string>
class DArgs;
bool I_VitaLauncher(DArgs *args);
std::string I_VitaCompanionFile(const char *iwad, const char *name);
#endif