"""Compare patch recognition and cached reader behavior using actual port code."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
def function(text,name):
 start=text.index(name);brace=text.index('{',start);depth=1;end=brace+1
 while depth:
  if text[end]=='{':depth+=1
  elif text[end]=='}':depth-=1
  end+=1
 return text[start:end]
current=(root/'src/textures/patchtexture.cpp').read_text()
old=subprocess.check_output(['git','show','HEAD:src/textures/patchtexture.cpp'],cwd=root).decode()
reader=(root/'src/posix/vita_texture_reader.h').read_text().replace('#include "files.h"','')
code=r'''
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <fstream>
#include <cassert>
#include <random>
#include <algorithm>
#include <limits>
typedef uint8_t BYTE;typedef uint32_t DWORD;
struct patch_t {int16_t width,height,leftoffset,topoffset;uint32_t columnofs[1];};
static int16_t LittleShort(int16_t x){return x;}
static uint32_t LittleLong(uint32_t x){return x;}
class FileReader {
protected:
 FILE *File=nullptr;bool CloseOnDestruct=false;long StartPos=0,FilePos=0,Length=0;
public:
 virtual~FileReader(){}
 long GetLength()const{return Length;}
 FILE*GetFile()const{return File;}
 virtual long Read(void*,long)=0;
 virtual long Seek(long,int)=0;
 virtual long Tell()const{return FilePos;}
 virtual char*Gets(char*,int){return nullptr;}
};
struct Source:FileReader {
 const std::vector<BYTE>&data;unsigned seeks=0,reads=0;uint64_t bytes=0;
 Source(const std::vector<BYTE>&d,bool disk=true):data(d){Length=d.size();if(disk)File=(FILE*)1;}
 long Read(void*p,long n){
  ++reads;n=std::min(n,Length-FilePos);if(n<=0)return 0;
  memcpy(p,data.data()+FilePos,n);FilePos+=n;bytes+=n;return n;
 }
 long Seek(long n,int origin){
  ++seeks;long pos=n+(origin==SEEK_CUR?FilePos:origin==SEEK_END?Length:0);
  if(pos<0)return -1;FilePos=pos;return 0;
 }
};
'''
code+=reader+'\n'+function(current,'static bool CheckIfPatch(FileReader & file)').replace('CheckIfPatch','NewCheck')+'\n'
code+=function(old,'static bool CheckIfPatch(FileReader & file)').replace('CheckIfPatch','OldCheck')
code+=r'''
int main(int argc,char**argv){
 std::mt19937 rng(1234);unsigned cases=0;uint64_t oldBytes=0,newBytes=0;
 auto compare=[&](const std::vector<BYTE>&data){
  Source original(data),changed(data);FVitaTextureReader cache(changed);
  bool result=NewCheck(cache);
  bool safe=true;
  if(data.size()>=8) {
   int width=int16_t(data[0]|(unsigned(data[1])<<8));
   int height=int16_t(data[2]|(unsigned(data[3])<<8));
   // The old validator reads out of bounds for certain truncated directories.
   if(width>0&&width<=2048&&height>0&&height<=2048&&width<long(data.size())/4&&8+4*width>long(data.size()))safe=false;
  }
  if(safe)assert(result==OldCheck(original));else assert(!result);
  oldBytes+=original.bytes;newBytes+=changed.bytes;++cases;
 };
 for(int i=0;i<2000;++i){
  std::vector<BYTE> data(rng()%12000);for(auto&b:data)b=rng();compare(data);
 }
 for(int width:{1,2,16,128,1024,2048}) {
  std::vector<BYTE> patch(8+4*width+100,0);
  patch[0]=width&255;patch[1]=width>>8;patch[2]=64;
  for(int n=0;n<width;++n){DWORD off=8+width*4;memcpy(patch.data()+8+4*n,&off,4);}
  compare(patch);
  patch[8]=0xff;patch[9]=0xff;patch[10]=0xff;patch[11]=0x7f;compare(patch);
 }
 std::vector<BYTE> data(15000);for(size_t i=0;i<data.size();++i)data[i]=i%251;
 for(bool disk:{false,true}){
  Source source(data,disk);FVitaTextureReader cache(source);
  for(int n=0;n<10000;++n){
   long pos=rng()%(data.size()+100);int length=rng()%5000;
   assert(!cache.Seek(pos,SEEK_SET));
   std::vector<BYTE> out(length);
   long got=cache.Read(out.data(),length),expected=std::min(long(length),std::max(0L,long(data.size())-pos));
   assert(got==expected && cache.Tell()==pos+got);
   if(got)assert(!memcmp(out.data(),data.data()+pos,got));
  }
  assert(!cache.Seek(-20,SEEK_END));BYTE tail[20];assert(cache.Read(tail,20)==20);
  assert(!memcmp(tail,data.data()+data.size()-20,20));
  assert(!cache.Seek(100,SEEK_SET));assert(!cache.Seek(-50,SEEK_CUR));
  BYTE value=0;assert(cache.Read(&value,1)==1&&value==data[50]);
  assert(cache.Seek(-1,SEEK_SET)==-1);
  assert(cache.Seek(1,999)==-1);
  assert(cache.Seek(100,SEEK_SET)==0);
  assert(cache.Seek(std::numeric_limits<long>::max(),SEEK_CUR)==-1);
 }
 Source many(data);FVitaTextureReader prefix(many);
 auto seeks=many.seeks,reads=many.reads;
 for(int n=0;n<100;++n){BYTE buf[16];prefix.Seek(0,SEEK_SET);assert(prefix.Read(buf,16)==16);}
 assert(many.seeks==seeks && many.reads==reads);
 for(int arg=1;arg<argc;++arg){
  std::ifstream wad(argv[arg],std::ios::binary);assert(wad);
  char magic[4];uint32_t count,offset;wad.read(magic,4);wad.read((char*)&count,4);wad.read((char*)&offset,4);
  struct Entry{uint32_t pos,size;char name[8];};std::vector<Entry> entries(count);
  wad.seekg(offset);wad.read((char*)entries.data(),count*16);assert(wad);
  uint64_t beforeOld=oldBytes,beforeNew=newBytes;
  for(auto&e:entries){std::vector<BYTE> lump(e.size);wad.seekg(e.pos);wad.read((char*)lump.data(),e.size);assert(wad);compare(lump);}
  printf("WAD %s: patch-check source bytes old=%llu new=%llu\n",argv[arg],
   (unsigned long long)(oldBytes-beforeOld),(unsigned long long)(newBytes-beforeNew));
 }
 printf("PASS: %u patch recognition cases; prefix/fallback reads, seek bounds, and repeated probes without source I/O\n",cases);
}
'''
with tempfile.TemporaryDirectory(prefix='vita-texture-reader-') as folder:
 p=Path(folder);(p/'test.cpp').write_text(code)
 subprocess.run(['g++','-std=c++17','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 fixtures=[root/'build-vita'/n for n in ['DOOM1.WAD','freedoom1.wad','freedoom2.wad','DBP37_AUGZEN.wad','labs/DoomMetalVol5_44100.wad']]
 subprocess.run([str(p/'test')]+[str(f) for f in fixtures if f.exists()],check=True)
