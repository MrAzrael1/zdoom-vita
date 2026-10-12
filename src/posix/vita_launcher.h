#ifndef ZDOOM_VITA_LAUNCHER_H
#define ZDOOM_VITA_LAUNCHER_H
#include <string>
class DArgs;
void I_VitaStartupImage(const unsigned int *pixels,int width,int height,bool fullWidth=false);
void I_VitaStartupSetSink(void (*sink)(const char*));
void I_VitaStartupReplay(void (*sink)(const char*));
void I_VitaStartupBegin();
void I_VitaStartupPrint(const char *text);
void I_VitaStartupEnd();
bool I_VitaLauncher(DArgs *args);
bool I_VitaLauncherProfile(bool &enabled);
std::string I_VitaCompanionFile(const char *iwad, const char *name);
#endif