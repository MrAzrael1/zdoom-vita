"""Exercise actual Vita directory traversal and ZDoom embedded-archive rules."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
directory=(root/'src/resourcefiles/file_directory.cpp').read_text()
a=directory.index('int FDirectory::AddDirectory',directory.index('#elif defined(__sun)'))
b=directory.index('\n#else',a)
# Ignore the internal #else in the Vita guard.
b=directory.index('\n#else\n\n//=',a)
loop=directory[a:b]
a=directory.index('bool FDirectory::Open')
b=directory.index('\n//====',a)
opened=directory[a:b]
resource=(root/'src/resourcefiles/resourcefile.cpp').read_text()
a=resource.index('void FResourceLump::CheckEmbedded()')
b=resource.index('\n//====',a)
embedded=resource[a:b]
code=r'''
#include <filesystem>
#include <fstream>
#include <cassert>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <dirent.h>
#include <sys/stat.h>
using FString=struct String;
struct String:std::string {
 using std::string::string;
 String(const std::string&s):std::string(s){}
 const char*GetChars()const{return c_str();}
 operator const char*()const{return c_str();}
};
template<class T>struct TArray:std::vector<T>{
 void Push(const T&v){this->push_back(v);}
 unsigned Size()const{return this->size();}
};
void Printf(const char*,...){}
DIR* ScanOpen(const char*p){
 if(std::string(p).find("unreadable/")!=std::string::npos){errno=EACCES;return nullptr;}
 return opendir(p);
}
#define opendir ScanOpen
#define ZD_VITA
struct FDirectoryLump{int unused;};
struct FDirectory{
 const char*Filename;unsigned NumLumps=0;int postprocess=0;
 std::vector<std::string> paths;std::vector<FDirectoryLump>Lumps;
 int AddDirectory(const char*);
 bool Open(bool);
 void AddEntry(const std::string&p,int){paths.emplace_back(p);Lumps.emplace_back();}
 void PostProcessArchive(void*,size_t){assert(NumLumps>0);++postprocess;}
};
enum{LUMPF_EMBEDDED=1};
struct FResourceLump{
 const char*FullName;void*Owner=nullptr;int Flags=0;char Name[8]={};
 void CheckEmbedded();
};
bool IsWadInFolder(void*,const char*){return false;}
'''
code+=loop+'\n'+opened+'\n'+embedded+r'''
int main(int argc,char**argv){
 namespace fs=std::filesystem;assert(argc==2);
 fs::path base=argv[1];fs::create_directories(base/"sprites");fs::create_directories(base/"unreadable");
 for(auto name:{"a.wad","b.wad","other.pk3","sprites/test.png","unreadable/hidden.wad",".ignored"})
  std::ofstream(base/name)<<"data";
 fs::create_symlink(base/"missing",base/"broken");
 mkfifo((base/"pipe").c_str(),0600);
 std::string path=base.string()+"/";FDirectory d;d.Filename=path.c_str();
 assert(d.Open(true));assert(d.NumLumps==4&&d.paths.size()==4&&d.postprocess==1);
 assert(fs::exists(base/"sprites/test.png")); // Loose resources retain relative layout.
 fs::create_directories(base/"empty");std::string empty=(base/"empty").string()+"/";
 FDirectory e;e.Filename=empty.c_str();assert(e.Open(true)&&e.NumLumps==0&&e.postprocess==0);
 FDirectory absent;std::string missing=(base/"missing").string()+"/";absent.Filename=missing.c_str();
 assert(absent.Open(true)&&absent.NumLumps==0&&absent.postprocess==0);
 // WADs at archive/folder root are embedded; PK3/ZIP and arbitrary nested WADs are not.
 for(const char*name:{"a.wad","b.wad"}){
  FResourceLump l;l.FullName=name;l.CheckEmbedded();assert(l.Flags&LUMPF_EMBEDDED);
 }
 for(const char*name:{"other.pk3","other.zip","sprites/nested.wad","patch.deh"}){
  FResourceLump l;l.FullName=name;l.CheckEmbedded();assert(!l.Flags);
 }
 puts("Vita directory recursion, missing files, unreadable children, empty folders and embedded rules passed");
}
'''
with tempfile.TemporaryDirectory() as temp:
 p=Path(temp);(p/'test.cpp').write_text(code)
 subprocess.run(['g++','-std=c++17','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),str(p/'assets')],check=True)

