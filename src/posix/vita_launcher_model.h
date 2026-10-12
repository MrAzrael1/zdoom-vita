#ifndef ZDOOM_VITA_LAUNCHER_MODEL_H
#define ZDOOM_VITA_LAUNCHER_MODEL_H
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
namespace VitaLauncher {
struct File {
 std::string path;
 bool iwad;
 bool directory;
 File(const std::string &p,bool game,bool dir=false):path(p),iwad(game),directory(dir){}
};
inline std::string Lower(std::string s) {
 for(size_t i=0;i<s.size();++i)s[i]=char(std::tolower((unsigned char)s[i]));
 return s;
}
inline std::string Resolve(const std::string &p, bool iwad) {
 const std::string data="ux0:data/zdoom-vita/";
 if(p.compare(0,data.size(),data)==0) {
  std::string relative=p.substr(data.size());
  if(relative.compare(0,5,"iwad/")!=0 && relative.compare(0,5,"pwad/")!=0)
   return data+(iwad?"iwad/":"pwad/")+relative;
 }
 if(p.compare(0,6,"app0:/")==0 || p.compare(0,4,"ux0:")==0)return p;
 return std::string(iwad?"ux0:data/zdoom-vita/iwad/":"ux0:data/zdoom-vita/pwad/")+p;
}
inline std::string DisplayPath(const std::string &p) {
 if(p.compare(0,11,"app0:/iwad/")==0)return p.substr(11);
 const std::string roots[]={"ux0:data/zdoom-vita/iwad/","ux0:data/zdoom-vita/pwad/"};
 for(const auto &root:roots)if(p.compare(0,root.size(),root)==0)return p.substr(root.size());
 return p;
}
inline std::string BaseName(const std::string &path) {
 return Lower(path.substr(path.find_last_of('/')+1));
}
// Deathkings needs the full Hexen IWAD; prefer its sibling, then the root, then subfolders.
inline std::string RequiredBase(const std::vector<File> &files,const std::string &selected) {
 if(BaseName(selected)!="hexdd.wad")return "";
 std::string directory=selected.substr(0,selected.find_last_of('/')+1),fallback;
 for(const File &f:files)if(f.iwad && BaseName(f.path)=="hexen.wad") {
  if(f.path.substr(0,f.path.find_last_of('/')+1)==directory)return f.path;
  if(fallback.empty() || f.path.substr(0,f.path.find_last_of('/')+1)=="ux0:data/zdoom-vita/iwad/")fallback=f.path;
 }
 return fallback;
}
// Hide redundant shareware choices without deleting any files.
inline void HideRedundantShareware(std::vector<File> &files,std::string &selected) {
 std::string fullDoom;
 for(const File &f:files)if(f.iwad && BaseName(f.path)=="doom.wad"){fullDoom=f.path;break;}
 if(fullDoom.empty())return;
 if(BaseName(selected)=="doom1.wad")selected=fullDoom;
 files.erase(std::remove_if(files.begin(),files.end(),[](const File &f){
  return f.iwad && BaseName(f.path)=="doom1.wad";
 }),files.end());
}
inline bool IsModExtension(const std::string &path) {
 size_t dot=path.find_last_of('.');
 if(dot==std::string::npos)return false;
 std::string ext=Lower(path.substr(dot));
 return ext==".wad" || ext==".pk3" || ext==".pk7" || ext==".zip" || ext==".deh" || ext==".bex";
}

inline std::string ConfigName(const std::string &text) {
 std::string name;
 for(unsigned char c:text)if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c==' ')
  if(name.size()<64)name+=char(c);
 size_t first=name.find_first_not_of(' ');
 if(first==std::string::npos)return "";
 return name.substr(first,name.find_last_not_of(' ')-first+1);
}
// Parse arguments directly; nothing is passed to a shell.
inline bool ParseArguments(const std::string &text,std::vector<std::string> &out) {
 std::vector<std::string> parsed;
 std::string token;char quote=0;bool started=false;
 for(size_t i=0;i<text.size();++i) {
  unsigned char c=text[i];
  if(c<32 || c>126)return false;
  if(c=='\\' && i+1<text.size() && (text[i+1]=='"' || text[i+1]=='\\')) {
   token+=text[++i];started=true;
  } else if(quote) {
   if(c==quote)quote=0;else token+=char(c);
  } else if(c=='"' || c=='\'') {quote=c;started=true;}
  else if(c==' ') {
   if(started){parsed.push_back(token);token.clear();started=false;}
  } else {token+=char(c);started=true;}
  if(parsed.size()>64)return false;
 }
 if(quote)return false;
 if(started)parsed.push_back(token);
 if(parsed.size()>64)return false;
 out.swap(parsed);return true;
}
inline bool ValidateConfig(const std::vector<std::string> &mods,const std::vector<File> &files,std::string &error) {
 unsigned archives=0,patches=0;
 std::vector<std::string> seen;
 for(const std::string &path:mods) {
  bool directory=false;
  for(const File &file:files)if(!file.iwad && file.path==path)directory=file.directory;
  if(!directory && !IsModExtension(path)){error="CONFIG CONTAINS AN UNSUPPORTED FILE";return false;}
  size_t dot=path.find_last_of('.');
  std::string ext=directory || dot==std::string::npos?"":Lower(path.substr(dot));
  if(!directory && (ext==".deh" || ext==".bex"))++patches;else ++archives;
  if(archives>8 || patches>1){error="CONFIG LIMIT: 8 MODS AND 1 DEH/BEX PATCH";return false;}
  if(std::find(seen.begin(),seen.end(),path)!=seen.end()){error="CONFIG CONTAINS A DUPLICATE MOD";return false;}
  seen.push_back(path);
  bool found=false;
  for(const File &file:files)if(!file.iwad && file.path==path){found=true;break;}
  if(!found){error="MISSING MOD: "+DisplayPath(path);return false;}
 }
 error.clear();return true;
}

struct Selection {
 std::string iwad;
 std::vector<std::string> mods;
 void Toggle(const std::string &path) {
  auto it=std::find(mods.begin(),mods.end(),path);
  if(it==mods.end())mods.push_back(path);else mods.erase(it);
 }
 int Order(const std::string &path) const {
  auto it=std::find(mods.begin(),mods.end(),path);
  return it==mods.end()?0:int(it-mods.begin())+1;
 }
 void Validate(const std::vector<File> &files) {
  bool valid=false;
  for(const File &f:files)if(f.iwad && f.path==iwad)valid=true;
  if(!valid) {
   iwad.clear();
   for(const File &f:files)if(f.iwad && Lower(DisplayPath(f.path))=="doom2.wad")iwad=f.path;
   if(iwad.empty())for(const File &f:files)if(f.iwad){iwad=f.path;break;}
  }
  std::vector<std::string> clean;
  for(const std::string &p:mods) {
   for(const File &f:files)if(!f.iwad && f.path==p && std::find(clean.begin(),clean.end(),p)==clean.end()){
    clean.push_back(p);break;
   }
  }
  mods.swap(clean);
 }
 std::vector<std::string> Arguments() const {
  std::vector<std::string> result;
  if(iwad.empty())return result;
  result.push_back("-iwad"); result.push_back(Resolve(iwad,true));
  std::vector<std::string> archives,patches;
  for(const std::string &p:mods) {
   size_t dot=p.find_last_of('.');
   std::string ext=dot==std::string::npos?"":Lower(p.substr(dot));
   (ext==".deh" || ext==".bex"?patches:archives).push_back(p);
  }
  if(!archives.empty()) {
   result.push_back("-file");
   for(const std::string &p:archives)result.push_back(Resolve(p,false));
  }
  if(!patches.empty()) {
   result.push_back("-deh");
   for(const std::string &p:patches)result.push_back(Resolve(p,false));
  }
  return result;
 }
};
}
#endif
