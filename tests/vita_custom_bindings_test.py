# Exercise actual game/custom-key archiving during repeated live Vita saves.
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
def extract(file,start,end):
 text=(root/file).read_text()
 a=text.index(start);return text[a:text.index(end,a)]
archive=extract('src/gameconfigfile.cpp','void FGameConfigFile::ArchiveGameData','void FGameConfigFile::ArchiveGlobalData')
bindings=extract('src/c_bind.cpp','void FKeyBindings::ArchiveBindings','//=============================================================================')
save=extract('src/keysections.cpp','static void DoSaveKeys','static int CurrentKeySection')
fixture=r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <map>
#include <vector>
#include <stdexcept>
#include <strings.h>
#define stricmp strcasecmp
#define mysnprintf snprintf
#define countof(a) (sizeof(a)/sizeof(a[0]))
bool throwOnArchive=false;
enum {NUM_KEYS=6,CVAR_ARCHIVE=1,CVAR_USERINFO=2,CVAR_MOD=4,CVAR_AUTO=8,CVAR_SERVERINFO=16};
struct FString {
 std::string s;
 FString(const char *v=""):s(v){}
 const char *GetChars() const{return s.c_str();}
 operator const char*()const{return s.c_str();}
 bool IsEmpty()const{return s.empty();}
 char operator[](int i)const{return s[i];}
 FString &operator=(const char*v){s=v;return *this;}
};
struct FConfigFile {
 std::string current;
 std::map<std::string,std::map<std::string,std::string>> sections;
 void SetSection(const char *s,bool=true){if(throwOnArchive && strcmp(s,"Doom.Bindings")==0)throw std::runtime_error("test archive failure");current=s;}
 void ClearCurrentSection(){sections[current].clear();}
 void ClearKey(const char *k){sections[current].erase(k);}
 void SetValueForKey(const char *k,const char *v){sections[current][k]=v;}
};
struct FGameConfigFile:FConfigFile {int sublen;bool bModSetup=true;void ArchiveGameData(const char*);};
struct FKeyBindings {
 FString Binds[NUM_KEYS];
 void ArchiveBindings(FConfigFile*,const char *matchcmd=nullptr);
};
FKeyBindings Bindings,DoubleBindings,AutomapBindings;
const char *ConfigKeyName(int i){static const char *keys[]={"unused","pad_x","pad_y","pad_a","dpadleft","pad_back"};return keys[i];}
template<class T>struct Array:std::vector<T>{unsigned Size()const{return this->size();}};
struct FKeyAction {FString mAction;};
struct FKeySection {FString mSection;Array<FKeyAction> mActions;};
Array<FKeySection> KeySections;
bool netgame=false;int consoleplayer=0;
void C_ArchiveCVars(FConfigFile*,int){}
void C_ArchiveAliases(FConfigFile*){}
'''
main=r'''
int main() {
 FKeySection actions;actions.mSection="BrutalDoomActions";
 actions.mActions.push_back(FKeyAction{"reloader"});
 actions.mActions.push_back(FKeyAction{"grenadetoss"});
 KeySections.push_back(actions);
 FGameConfigFile config;
 Bindings.Binds[1]="reloader";Bindings.Binds[2]="grenadetoss";Bindings.Binds[3]="+use";
 DoubleBindings.Binds[1]="reloader";AutomapBindings.Binds[5]="togglemap";
 config.sections["Doom.Bindings"]["pad_x"]="+jump";
 for(int i=0;i<4;++i) {
  config.ArchiveGameData("Doom");
  assert(std::string(Bindings.Binds[1])=="reloader");
  assert(std::string(Bindings.Binds[2])=="grenadetoss");
  assert(std::string(Bindings.Binds[3])=="+use");
  assert(std::string(DoubleBindings.Binds[1])=="reloader");
  assert(config.sections["Doom.BrutalDoomActions.Bindings"]["pad_x"]=="reloader");
  assert(config.sections["Doom.BrutalDoomActions.Bindings"]["pad_y"]=="grenadetoss");
  assert(config.sections["Doom.BrutalDoomActions.DoubleBindings"]["pad_x"]=="reloader");
  assert(config.sections["Doom.Bindings"]["pad_a"]=="+use");
  assert(config.sections["Doom.AutomapBindings"]["pad_back"]=="togglemap");
 }
 // Changed bindings replace stale custom keys on the next save.
 Bindings.Binds[1]="+jump";DoubleBindings.Binds[1]="";
 config.ArchiveGameData("Doom");
 assert(config.sections["Doom.BrutalDoomActions.Bindings"].count("pad_x")==0);
 assert(config.sections["Doom.BrutalDoomActions.DoubleBindings"].count("pad_x")==0);
 assert(config.sections["Doom.Bindings"]["pad_x"]=="+jump");
 assert(std::string(Bindings.Binds[2])=="grenadetoss");
 // A serialization exception still preserves live bindings.
 throwOnArchive=true;
 try {config.ArchiveGameData("Doom");assert(false);}catch(const std::runtime_error&){}
 assert(std::string(Bindings.Binds[1])=="+jump");
 assert(std::string(Bindings.Binds[2])=="grenadetoss");
 puts("PASS: repeated custom-key saves, live normal/double bindings, stale-key removal and exception recovery");
}
'''
with tempfile.TemporaryDirectory(prefix='vita-custom-bindings-') as folder:
 p=Path(folder);(p/'test.cpp').write_text(fixture+bindings+save+archive+main)
 subprocess.run(['g++','-std=c++11','-DZD_VITA','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)