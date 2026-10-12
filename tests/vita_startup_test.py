"""Run the actual Vita startup adapter against deterministic assets/audio/display."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
inc=(root/'src/posix/sdl/vita_startup.inc').read_text()
inc='\n'.join(l for l in inc.splitlines() if not l.startswith('#include'))
code=r'''
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include "posix/vita_startup_pixels.h"
using Uint32=uint32_t;
static Uint32 ticks=0;
Uint32 SDL_GetTicks(){return ticks+=100;}
struct String:std::string {
 using std::string::string;
 String&operator=(const char*s){std::string::operator=(s);return *this;}
 bool IsNotEmpty()const{return !empty();}
 const char*GetChars()const{return c_str();}
};
struct FStartupInfo{
 enum{DefaultStartup,DoomStartup,HereticStartup,HexenStartup,StrifeStartup};
 int Type=DefaultStartup; String Song,Name="Test";
 unsigned FgColor=0xffffff,BkColor=0;
} DoomStartupInfo;
enum{GAME_Doom,GAME_Heretic,GAME_Hexen,GAME_Strife};
struct{int gametype=GAME_Doom;}gameinfo;
struct Arguments{bool nostart=false;bool CheckParm(const char*){return nostart;}} arguments,*Args=&arguments;
struct Wad {
 std::vector<std::string> names; std::vector<std::vector<unsigned char>> data;
 int CheckNumForName(const char*n){for(size_t i=0;i<names.size();++i)if(names[i]==n)return i;return -1;}
 int CheckNumForFullName(const char*n){return CheckNumForName(n);}
 int LumpLength(int n){return data.at(n).size();}
 void ReadLump(int n,void*p){memcpy(p,data.at(n).data(),data.at(n).size());}
 void Add(const char*n,size_t size,unsigned char val=0){names.push_back(n);data.emplace_back(size,val);}
} Wads;
struct FTTYStartupScreen {
 int MaxPos,CurPos=0,NotchPos=0,NetCurPos=0; bool DidNetInit=false;
 FTTYStartupScreen(int n):MaxPos(n){}
 virtual~FTTYStartupScreen(){}
 void NetProgress(int n){DidNetInit=true;NetCurPos=n?n:NetCurPos+1;}
 void NetDone(){DidNetInit=false;}
};
void(*sink)(const char*)=nullptr;
std::vector<uint32_t> frame;int frameWidth=0,frameHeight=0,draws=0;bool fullWidth=false;
void I_VitaStartupSetSink(void(*s)(const char*)){sink=s;}
void I_VitaStartupReplay(void(*s)(const char*)){s("W_Init: Init WADfiles.\n");}
void I_VitaStartupImage(const unsigned int*p,int w,int h,bool wide){
 fullWidth=wide;
 frame.assign(p,p+w*h);frameWidth=w;frameHeight=h;++draws;
}
void I_VitaStartupEnd(){sink=nullptr;}
int pumps=0,stops=0,musicCalls=0;std::string music;
void*GSnd=(void*)1;
void S_UpdateMusic(){++pumps;}
void S_StopMusic(bool){++stops;}
bool S_ChangeMusic(const char*s,int,bool,bool){++musicCalls;music=s;return true;}
enum{CHAN_BODY,ATTN_NONE,ATTN_NORM};
std::vector<std::string> sounds;
void S_Sound(int,const char*s,int,int){sounds.emplace_back(s);}
void Reset(){
 Wads=Wad();DoomStartupInfo.Type=FStartupInfo::DefaultStartup;
 DoomStartupInfo.Song="";gameinfo.gametype=GAME_Doom;
 arguments.nostart=false;draws=musicCalls=stops=pumps=0;sounds.clear();frame.clear();
 Wads.Add("vga-rom-font.16",4096,0x80);
}
'''
code+=inc+r'''
int main(){
 // Basic console uses original VGA glyphs, messages, title and actual progress.
 Reset();
 {FVitaStartupScreen s(4);assert(frameWidth==640&&frameHeight==400&&sink&&fullWidth);
 sink("LoadActors: Load actor definitions.\n");
 for(int i=0;i<10;++i)s.Progress();
 assert(frame[386*640+8]==VitaStartup::Bitmap::RGB(170,170,170));
 assert(musicCalls==0&&pumps>0);}
 assert(!sink);
 // Hexen planar background, packed notches, original song and tick sounds.
 Reset();gameinfo.gametype=GAME_Hexen;
 Wads.Add("STARTUP",153648);auto &data=Wads.data.back();
 data[0]=63; // Palette zero is red.
 Wads.Add("NOTCH",184,255);Wads.Add("NETNOTCH",32,255);
 {FVitaStartupScreen s(32);assert(frameWidth==640&&frameHeight==480&&!fullWidth);
 assert(frame[0]==VitaStartup::Bitmap::RGB(255,0,0));
 for(int i=0;i<32;++i)s.Progress();
 assert(musicCalls==1&&music=="orb");
 assert(std::count(sounds.begin(),sounds.end(),"StartupTick")==32);
 s.NetProgress(2);s.NetDone();
 assert(std::find(sounds.begin(),sounds.end(),"misc/netnotch")!=sounds.end());
 assert(std::find(sounds.begin(),sounds.end(),"PickupWeapon")!=sounds.end());}
 assert(stops==1);
 // Heretic uses LOADING text/attributes and status positions.
 Reset();gameinfo.gametype=GAME_Heretic;Wads.Add("LOADING",4000);
 auto &loading=Wads.data.back();for(int i=0;i<2000;++i){loading[i*2]='A';loading[i*2+1]=0x1f;}
 {FVitaStartupScreen s(404);assert(frameWidth==640&&frameHeight==400);
 assert(frame[0]==VitaStartup::Bitmap::RGB(255,255,255));
 assert(frame[1]==VitaStartup::Bitmap::RGB(0,0,170));
 s.LoadingStatus("Loading graphics",0x3f);s.AppendStatusLine("DeathMatch...");
 for(int i=0;i<404;++i)s.Progress();
 assert(frame[224*640+112]==VitaStartup::Bitmap::RGB(85,255,85));}
 // Strife's cropped background and overlays; missing overlays remain safe.
 Reset();gameinfo.gametype=GAME_Strife;
 Wads.Add("STARTUP0",64000,1);Wads.Add("PLAYPAL",768);
 Wads.data.back()[3]=123;
 {FVitaStartupScreen s(184);assert(frameWidth==320&&frameHeight==200);
 assert(frame[41*320]==VitaStartup::Bitmap::RGB(123,0,0));
 for(int i=0;i<184;++i)s.Progress();}
 // Invalid graphical assets fall back to a real console.
 Reset();gameinfo.gametype=GAME_Hexen;Wads.Add("STARTUP",12);
 {FVitaStartupScreen s(1);assert(frameWidth==640&&frameHeight==400);}
 Reset();gameinfo.gametype=GAME_Hexen;arguments.nostart=true;
 {FVitaStartupScreen s(0);s.Progress();assert(frameHeight==400&&musicCalls==0);}
 // Missing font preserves the previous scrolling console rather than blanking it.
 Reset();Wads=Wad();
 {FVitaStartupScreen s(1);s.Progress();assert(draws==0&&!sink);}
 // Full-name custom song is passed intact, exactly once.
 Reset();DoomStartupInfo.Song="custom/start.mid";
 {FVitaStartupScreen s(4);for(int i=0;i<4;++i)s.Progress();
 assert(music=="custom/start.mid"&&musicCalls==1);}
 // Planar bit significance and packed high/low nibble order.
 VitaStartup::Bitmap b;b.Resize(8,1);
 for(int i=0;i<16;++i)b.palette[i]=i;
 unsigned char planes[]={0x80,0x40,0x20,0x10};b.Planar(planes);
 assert(b.pixels[0]==1&&b.pixels[1]==2&&b.pixels[2]==4&&b.pixels[3]==8);
 b.Block(std::vector<unsigned char>{0x12,0x48},0,0,4,1,true);
 assert(b.pixels[0]==1&&b.pixels[1]==2&&b.pixels[2]==4&&b.pixels[3]==8);
 // Clipping and malformed asset sizes never write out of bounds.
 b.Block(std::vector<unsigned char>{1},0,0,16,23,true);
 b.Fill(-5,-10,30,20,42);
 puts("Vita original startup content, progress, sounds and fallback tests passed");
}
'''
with tempfile.TemporaryDirectory() as temp:
 p=Path(temp);(p/'test.cpp').write_text(code)
 subprocess.run(['g++','-std=c++17','-fsanitize=address,undefined','-I'+str(root/'src'),
 str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
