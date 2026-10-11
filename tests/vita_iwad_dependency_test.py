"""Exercise the actual Vita required-IWAD catalog injection with host sanitizers."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'src/d_iwad.cpp').read_text()
pos=source.index('const char *specifiedBase = Args->CheckValue("-vita-base-iwad");')
begin=source.rfind('#ifdef ZD_VITA',0,pos)+len('#ifdef ZD_VITA')
end=source.index('#endif',pos)
body=source[begin:end]
fixture=r'''
#include <string>
#include <vector>
#include <cstring>
#include <strings.h>
#include <cassert>
#define stricmp strcasecmp
#define I_VitaTrace(...) ((void)0)
struct String:std::string {
 using std::string::string;
 using std::string::operator=;
 bool IsEmpty()const{return empty();}
 bool IsNotEmpty()const{return !empty();}
 const char*GetChars()const{return c_str();}
 int Compare(const String&s)const{return compare(s);}
};
template<class T>struct Array:std::vector<T>{
 using std::vector<T>::vector;size_t Size()const{return this->size();}
};
struct Info{String Required,Name;};
struct Wad{String Path,Name;int Type=-1;};
struct ArgsType{
 const char *specified=nullptr;
 const char*CheckValue(const char*)const{return specified;}
};
ArgsType arg;ArgsType *Args=&arg;
std::string fallback;
std::string I_VitaCompanionFile(const char*,const char*){return fallback;}
int ScanIWAD(const char*p) {
 if(!strcmp(p,"valid"))return 1;
 if(!strcmp(p,"demo"))return 2;
 return -1;
}
void Inject(const char*iwadparm,Array<Wad>&wads,Array<Info>&mIWads,Array<String>&mIWadNames) {
 BODY
}
int main() {
 Array<Info> infos={{"Hexen: Beyond Heretic","Deathkings"},{"","Hexen: Beyond Heretic"},{"","Hexen: Demo Version"}};
 Array<String> names={"HEXDD.WAD","hexen.wad","doom.wad"};
 auto fresh=[](){Array<Wad>w(3);w[0].Path="expansions/HEXDD.WAD";w[0].Type=0;return w;};
 auto w=fresh();arg.specified="valid";Inject("HEXDD.WAD",w,infos,names);
 assert(w[1].Type==1 && w[1].Path=="valid" && w[1].Name=="Hexen: Beyond Heretic");
 assert(w[0].Path=="expansions/HEXDD.WAD" && w[0].Type==0 && w[2].Path.empty());
 w=fresh();arg.specified=nullptr;fallback="valid";Inject("HEXDD.WAD",w,infos,names);assert(w[1].Type==1);
 w=fresh();arg.specified="demo";Inject("HEXDD.WAD",w,infos,names);assert(w[1].Path.empty());
 w=fresh();arg.specified="missing";Inject("HEXDD.WAD",w,infos,names);assert(w[1].Path.empty());
 w=fresh();arg.specified="valid";Inject(nullptr,w,infos,names);assert(w[1].Path.empty());
 w=fresh();w[0].Type=1;Inject("HEXEN.WAD",w,infos,names);assert(w[1].Path.empty());
}
'''
with tempfile.TemporaryDirectory(prefix='vita-iwad-dependency-') as temp:
 p=Path(temp);(p/'test.cpp').write_text(fixture.replace('BODY',body))
 subprocess.run(['g++','-std=c++17','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: actual engine dependency injection, base-game validation, fallback and expansion identity')