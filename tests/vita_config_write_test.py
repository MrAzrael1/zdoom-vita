# Exercise the actual WriteConfigFile implementation with lightweight host fixtures.
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
source=(root/"src/configfile.cpp").read_text()
start=source.index("bool FConfigFile::WriteConfigFile () const")
end=source.index("//====================================================================",start)
method=source[start:end]
fixture=r'''
#include <cstdio>
#include <cstring>
#include <string>
#include <cassert>
#include <sys/stat.h>
struct Text {
 std::string text;
 Text(const char *s=""):text(s){}
 const char *GetChars() const {return text.c_str();}
 operator const char*() const {return GetChars();}
 bool IsNotEmpty() const {return !text.empty();}
};
struct FConfigEntry {const char *Key,*Value; FConfigEntry *Next;};
struct FConfigSection {Text SectionName,Note;FConfigEntry *RootEntry;FConfigSection *Next;};
struct FConfigFile {
 bool OkayToWrite=true,FileExisted=false;
 Text PathName; FConfigSection *Sections=nullptr;
 bool WriteConfigFile() const;
 void WriteCommentHeader(FILE *f) const {fputs("# test\n",f);}
 const char *GenerateEndTag(const char*) const {return "END";}
};
'''
main=r'''
int main(int argc,char **argv) {
 FConfigEntry binding={"Pad_A","+use",nullptr};
 FConfigEntry name={"name","VitaPlayer",&binding};
 FConfigSection section={"Doom.Player",{},&name,nullptr};
 FConfigFile config;config.PathName=argv[1];config.Sections=&section;
 assert(config.WriteConfigFile());
 struct stat first,second;assert(stat(argv[1],&first)==0);
 assert(config.WriteConfigFile());assert(stat(argv[1],&second)==0);
 assert(first.st_mtim.tv_sec==second.st_mtim.tv_sec && first.st_mtim.tv_nsec==second.st_mtim.tv_nsec);
 name.Value="SavedPlayer";binding.Value="+jump";assert(config.WriteConfigFile());
 char data[512]={};FILE *f=fopen(argv[1],"r");assert(f);fread(data,1,sizeof(data)-1,f);fclose(f);
 assert(strstr(data,"name=SavedPlayer") && strstr(data,"Pad_A=+jump"));
 config.PathName="/nonexistent-zdoom-test-parent/zdoom.ini";
 assert(!config.WriteConfigFile());
 puts("PASS: player/binding serialization, unchanged-write suppression, changed settings and open failure");
}
'''
with tempfile.TemporaryDirectory(prefix="zdoom-config-test-") as folder:
 path=Path(folder); (path/"test.cpp").write_text(fixture+method+main)
 subprocess.run(["g++","-std=c++11","-DZD_VITA","-fsanitize=address,undefined",str(path/"test.cpp"),"-o",str(path/"test")],check=True)
 subprocess.run([str(path/"test"),str(path/"zdoom.ini")],check=True)
