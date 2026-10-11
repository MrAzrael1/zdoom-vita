// Pre-engine Vita file selector. No engine graphics or configuration required.
#include <SDL.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <cstdio>
#include <cstring>
#include "m_argv.h"
#include "vita_debug.h"
#include "vita_launcher.h"
#include "vita_launcher_model.h"
namespace {
using namespace VitaLauncher;
const char *DataRoot="ux0:data/zdoom-vita/";
const char *IwadRoot="ux0:data/zdoom-vita/iwad/";
const char *PwadRoot="ux0:data/zdoom-vita/pwad/";
const int Rows=10, RowY=116, RowHeight=32;
void Scan(const std::string &root,const std::string &relative, int depth, bool wantIwad, std::vector<File> &files, bool &limited) {
 if(depth>4 || files.size()>=512){limited=true;return;}
 std::string directory=root+relative;
 SceUID handle=sceIoDopen(directory.c_str());
 if(handle<0){I_VitaTrace("[Vita] Launcher: cannot scan %s (%d)\n",directory.c_str(),handle);return;}
 SceIoDirent entry;
 for(;;) {
  memset(&entry,0,sizeof(entry));
  int result=sceIoDread(handle,&entry);
  if(result<=0)break;
  std::string name=entry.d_name;
  if(name.empty() || name[0]=='.' || name.find_first_of("\r\n")!=std::string::npos)continue;
  std::string path=relative+name;
  if(path.size()>900){limited=true;continue;}
  if(SCE_S_ISDIR(entry.d_stat.st_mode)){Scan(root,path+"/",depth+1,wantIwad,files,limited);continue;}
  if(!IsModExtension(path))continue;
  bool iwad=false;
  if(Lower(path).substr(Lower(path).size()-4)==".wad") {
   char header[4]={};
   FILE *f=fopen((root+path).c_str(),"rb");
   if(!f)continue;
   size_t n=fread(header,1,4,f);fclose(f);
   if(n!=4 || (memcmp(header,"IWAD",4)!=0 && memcmp(header,"PWAD",4)!=0))continue;
   iwad=memcmp(header,"IWAD",4)==0;
  }
  if(wantIwad && (!iwad || Lower(name)=="voices.wad"))continue;
  files.push_back({root+path,wantIwad});
  if(files.size()>=512){limited=true;break;}
 }
 sceIoDclose(handle);
}
void Load(Selection &selection) {
 FILE *f=fopen("ux0:data/zdoom-vita/launcher.cfg","r");
 if(!f)return;
 char line[1024];
 while(fgets(line,sizeof(line),f)) {
  size_t n=strlen(line);
  while(n && (line[n-1]=='\n'||line[n-1]=='\r'))line[--n]=0;
  if(strncmp(line,"iwad=",5)==0)selection.iwad=line+5;
  else if(strncmp(line,"mod=",4)==0 && selection.mods.size()<512)selection.mods.push_back(line+4);
 }
 fclose(f);
}
void Save(const Selection &selection) {
 FILE *f=fopen("ux0:data/zdoom-vita/launcher.cfg","w");
 if(!f){I_VitaTrace("[Vita] Launcher: selection save failed\n");return;}
 fprintf(f,"iwad=%s\n",selection.iwad.c_str());
 for(const std::string &p:selection.mods)fprintf(f,"mod=%s\n",p.c_str());
 bool okay=!ferror(f);
 if(fflush(f)!=0)okay=false;
 if(fclose(f)!=0)okay=false;
 I_VitaTrace("[Vita] Launcher: selection save %s\n",okay?"OK":"FAILED");
}
// Small original 5x7 alphabet. Lowercase filenames are displayed in uppercase;
// actual argument paths preserve their exact spelling.
const unsigned char Glyphs[][7]={
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
 {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
 {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
 {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
 {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
 {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
 {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
 {14,17,17,15,1,1,14},
 {0,0,0,31,0,0,0},{0,0,0,0,0,0,31},{0,0,0,0,0,12,12},
 {0,4,4,0,4,4,0},{1,2,2,4,8,8,16},{6,8,16,16,16,8,6},
 {12,2,1,1,1,2,12},{14,8,8,8,8,8,14},{14,2,2,2,2,2,14},
 {0,4,4,31,4,4,0},{0,0,31,0,31,0,0},{14,17,1,2,4,0,4}
};

// Offline-rasterized DooM.ttf; original alphabet remains a fallback.
SDL_Texture *FontTexture=NULL;
int FontWidth[96];
void LoadFont(SDL_Renderer *r) {
 unsigned char alpha[512*192];
 FILE *f=fopen("app0:/launcher-font.bin","rb");
 if(!f)return;
 bool okay=fread(alpha,1,sizeof(alpha),f)==sizeof(alpha);fclose(f);
 if(!okay)return;
 std::vector<Uint32> pixels(512*192);
 for(size_t i=0;i<pixels.size();++i)pixels[i]=(Uint32(alpha[i])<<24)|0x00ffffff;
 for(int i=0;i<96;++i) {
  int width=0;
  for(int y=0;y<32;++y)for(int x=0;x<32;++x)
   if(alpha[((i/16)*32+y)*512+(i%16)*32+x])width=std::max(width,x+1);
  FontWidth[i]=width?width:10;
 }
 FontTexture=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,512,192);
 if(FontTexture && SDL_UpdateTexture(FontTexture,NULL,pixels.data(),512*sizeof(Uint32))<0) {
  SDL_DestroyTexture(FontTexture);FontTexture=NULL;
 }
 if(FontTexture) {
  SDL_SetTextureBlendMode(FontTexture,SDL_BLENDMODE_BLEND);
  SDL_SetTextureColorMod(FontTexture,200,32,24);
  I_VitaTrace("[Vita] Launcher: DooM font enabled\n");
 }
}

void Text(SDL_Renderer *r,int x,int y,const std::string &text,int scale,int limit=74) {
 if(FontTexture) {
  int right=std::min(928,x+limit*6*scale),cell=scale*12;
  int total=0;
  for(unsigned char c:text) {
   c=c<128?(unsigned char)std::toupper(c):'?';
   if(c<32 || c>127)c='?';
   total+=(FontWidth[c-32]*cell+31)/32+2;
  }
  if(total>right-x)cell=std::max(12,cell*(right-x)/total);
  for(unsigned char original:text) {
   unsigned char c=original<128?(unsigned char)std::toupper(original):'?';
   if(c<32 || c>127)c='?';
   int i=c-32,advance=(FontWidth[i]*cell+31)/32+2;
   if(x+advance>right)break;
   SDL_Rect source={(i%16)*32,(i/16)*32,32,32},dest={x,y-5,cell,cell};
   SDL_RenderCopy(r,FontTexture,&source,&dest);
   x+=advance;
  }
  return;
 }
 int chars=0;
 for(unsigned char original:text) {
  if(chars++>=limit)break;
  unsigned char c=original<128?(unsigned char)std::toupper(original):'?';
  int index=-1;
  if(c>='A'&&c<='Z')index=c-'A';
  else if(c>='0'&&c<='9')index=26+c-'0';
  else {
   const char *punct="-_.:/()[]+=?";
   const char *p=strchr(punct,c);
   if(p && c)index=36+int(p-punct);
  }
  if(index>=0)for(int row=0;row<7;++row)for(int col=0;col<5;++col)
   if(Glyphs[index][row]&(1<<(4-col))) {
    SDL_Rect pixel={x+col*scale,y+row*scale,scale,scale};
    SDL_RenderFillRect(r,&pixel);
   }
  x+=6*scale;
 }
}
void Color(SDL_Renderer *r,int a,int b,int c){SDL_SetRenderDrawColor(r,a,b,c,255);}
void Rect(SDL_Renderer*r,int x,int y,int w,int h){SDL_Rect b={x,y,w,h};SDL_RenderFillRect(r,&b);}
}
bool I_VitaLauncher(DArgs *args) {
 std::vector<File> files;
 bool limited=false;
 // Reserve bundled choices before the user-file scan reaches its safety limit.
 Scan("app0:/iwad/","",0,true,files,limited);
 Scan(IwadRoot,"",0,true,files,limited);
 Scan(PwadRoot,"",0,false,files,limited);
 std::sort(files.begin(),files.end(),[](const File&a,const File&b){return Lower(a.path)<Lower(b.path);});
 Selection selection;
 Load(selection);
 selection.iwad=Resolve(selection.iwad,true);
 for(std::string &p:selection.mods)p=Resolve(p,false);
 HideRedundantShareware(files,selection.iwad);
 selection.Validate(files);
 std::vector<int> lists[2];
 for(size_t i=0;i<files.size();++i)lists[files[i].iwad?0:1].push_back(int(i));
 I_VitaTrace("[Vita] Launcher: %d IWADs, %d mods, limited=%d\n",
  int(lists[0].size()),int(lists[1].size()),limited);
 if(SDL_InitSubSystem(SDL_INIT_VIDEO|SDL_INIT_JOYSTICK)<0)return false;
 SDL_Window *window=SDL_CreateWindow("ZDoom Vita",SDL_WINDOWPOS_UNDEFINED,
  SDL_WINDOWPOS_UNDEFINED,960,544,SDL_WINDOW_FULLSCREEN);
 SDL_Renderer *renderer=window?SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC):NULL;
 if(!renderer) {
  I_VitaTrace("[Vita] Launcher: graphics failed: %s\n",SDL_GetError());
  if(window)SDL_DestroyWindow(window);
  return false;
 }
 SDL_RenderSetLogicalSize(renderer,960,544);
 LoadFont(renderer);
 SDL_Joystick *joystick=SDL_NumJoysticks()>0?SDL_JoystickOpen(0):NULL;
 int tab=0,focus[2]={0,0},top[2]={0,0};
 for(size_t i=0;i<lists[0].size();++i)if(files[lists[0][i]].path==selection.iwad)focus[0]=int(i);
 bool running=true,launch=false;
 auto choose=[&]() {
  if(lists[tab].empty())return;
  const File &f=files[lists[tab][focus[tab]]];
  if(tab==0)selection.iwad=f.path;else selection.Toggle(f.path);
 };
 while(running) {
  SDL_Event e;
  while(SDL_PollEvent(&e)) {
   if(e.type==SDL_QUIT){running=false;break;}
   int action=-1;
   if(e.type==SDL_JOYBUTTONDOWN) {
    int b=e.jbutton.button;
    if(b==8)action=0;else if(b==6)action=1;else if(b==7||b==4)action=2;
    else if(b==9||b==5)action=3;else if(b==2)action=4;
    else if(b==3||b==11)action=5;else if(b==0)action=6;else if(b==1)action=7;
   } else if(e.type==SDL_KEYDOWN) {
    switch(e.key.keysym.sym) {
     case SDLK_UP:action=0;break;case SDLK_DOWN:action=1;break;
     case SDLK_LEFT:action=2;break;case SDLK_RIGHT:case SDLK_TAB:action=3;break;
     case SDLK_RETURN:action=4;break;case SDLK_F5:action=5;break;case SDLK_ESCAPE:action=7;break;
    }
   } else if(e.type==SDL_FINGERDOWN && e.tfinger.touchId==1) {
    int x=int(e.tfinger.x*960),y=int(e.tfinger.y*544);
    if(y>=70&&y<105)tab=x<480?0:1;
    else if(y>=RowY&&y<RowY+Rows*RowHeight) {
     int i=top[tab]+(y-RowY)/RowHeight;
     if(i<int(lists[tab].size())){focus[tab]=i;choose();}
    } else if(y>=478&&y<513 && x>=680)action=5;
   }
   if(action==0&&!lists[tab].empty())focus[tab]=(focus[tab]+int(lists[tab].size())-1)%lists[tab].size();
   if(action==1&&!lists[tab].empty())focus[tab]=(focus[tab]+1)%lists[tab].size();
   if(action==2)tab=0;
   if(action==3)tab=1;
   if(action==4)choose();
   if(action==5&&!selection.iwad.empty()){launch=true;running=false;}
   if(action==6)selection.mods.clear();
   if(action==7){if(tab)tab=0;else running=false;}
  }
  if(focus[tab]<top[tab])top[tab]=focus[tab];
  if(focus[tab]>=top[tab]+Rows)top[tab]=focus[tab]-Rows+1;
  Color(renderer,16,19,27);SDL_RenderClear(renderer);
  Color(renderer,230,232,238);Text(renderer,32,22,"ZDOOM VITA",3);
  Color(renderer,139,151,175);Text(renderer,32,51,"SELECT YOUR GAME AND OPTIONAL MODS",2);
  Color(renderer,tab==0?70:34,tab==0?83:40,tab==0?116:55);Rect(renderer,32,70,440,32);
  Color(renderer,tab==1?70:34,tab==1?83:40,tab==1?116:55);Rect(renderer,488,70,440,32);
  Color(renderer,240,240,245);Text(renderer,48,80,"IWADS",2);Text(renderer,504,80,"MODS - SELECT IN LOAD ORDER",2);
  for(int row=0;row<Rows;++row) {
   int i=top[tab]+row;
   if(i>=int(lists[tab].size()))break;
   const File &f=files[lists[tab][i]];
   if(i==focus[tab]){Color(renderer,46,54,73);Rect(renderer,32,RowY+row*RowHeight,896,30);}
   int order=selection.Order(f.path);
   std::string label;
   if(tab==0)label=(selection.iwad==f.path?"[X] ":"[ ] ")+DisplayPath(f.path);
   else label=(order?"["+std::to_string(order)+"] ":"[ ] ")+DisplayPath(f.path);
   Color(renderer,230,232,238);Text(renderer,44,RowY+row*RowHeight+8,label,2,72);
  }
  if(lists[tab].empty()) {
   Color(renderer,230,232,238);Text(renderer,44,148,tab?"NO MOD FILES FOUND":"NO IWAD FILES FOUND",2);
   Text(renderer,44,184,tab?"PUT MODS IN UX0:DATA/ZDOOM-VITA/PWAD":"PUT GAMES IN UX0:DATA/ZDOOM-VITA/IWAD",2);
  }
  Color(renderer,139,151,175);
  Text(renderer,32,448,limited?"SCAN LIMIT REACHED - FIRST 512 FILES, 4 SUBFOLDER LEVELS":tab?"MODS: UX0:DATA/ZDOOM-VITA/PWAD":"GAMES: USER IWAD FOLDER AND BUNDLED GAMES",2);
  Text(renderer,32,475,"IWAD: "+(selection.iwad.empty()?std::string("NONE"):DisplayPath(selection.iwad)),2,52);
  Text(renderer,32,495,"MODS: "+std::to_string(selection.mods.size())+" - TRIANGLE CLEARS",2,52);
  Color(renderer,selection.iwad.empty()?50:55,selection.iwad.empty()?50:104,selection.iwad.empty()?50:78);
  Rect(renderer,680,478,248,35);Color(renderer,245,245,245);Text(renderer,708,488,"START GAME",2);
  Color(renderer,139,151,175);Text(renderer,32,526,"D-PAD BROWSE  CROSS SELECT  SQUARE/START LAUNCH  CIRCLE BACK/EXIT",2);
  SDL_RenderPresent(renderer);
  SDL_Delay(8);
 }
 if(joystick)SDL_JoystickClose(joystick);
 if(FontTexture){SDL_DestroyTexture(FontTexture);FontTexture=NULL;}
 SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);
 SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT);
 if(!launch)return false;
 Save(selection);
 Selection launchSelection=selection;
 std::string base=Lower(selection.iwad.substr(selection.iwad.find_last_of('/')+1));
 if(base=="strife1.wad") {
  std::string voice=I_VitaCompanionFile(Resolve(selection.iwad,true).c_str(),"voices.wad");
  for(auto it=launchSelection.mods.begin();it!=launchSelection.mods.end();) {
   if(Resolve(*it,false)==voice)it=launchSelection.mods.erase(it);else ++it;
  }
  if(!voice.empty())I_VitaTrace("[Vita] Launcher: Strife voices autoload=%s\n",voice.c_str());
 }
 for(const std::string &arg:launchSelection.Arguments())args->AppendArg(arg.c_str());
 I_VitaTrace("[Vita] Launcher: IWAD=%s mods=%d\n",selection.iwad.c_str(),int(selection.mods.size()));
 for(const std::string &mod:selection.mods)I_VitaTrace("[Vita] Launcher: mod=%s\n",mod.c_str());
 return true;
}
// Strife requests voices.wad through its existing IWAD definition: resolve once.
std::string I_VitaCompanionFile(const char *iwad,const char *name) {
 std::string path=iwad;
 size_t slash=path.find_last_of('/');
 std::string folders[4]={slash==std::string::npos?"":path.substr(0,slash+1),IwadRoot,PwadRoot,DataRoot};
 for(int i=0;i<4;++i) {
  if(i==1 && folders[0]==folders[1])continue;
  SceUID handle=sceIoDopen(folders[i].c_str());
  if(handle<0)continue;
  std::string found;
  SceIoDirent entry;
  for(;;) {
   memset(&entry,0,sizeof(entry));
   if(sceIoDread(handle,&entry)<=0)break;
   if(!SCE_S_ISDIR(entry.d_stat.st_mode) && VitaLauncher::Lower(entry.d_name)==VitaLauncher::Lower(name)) {
    found=folders[i]+entry.d_name;break;
   }
  }
  sceIoDclose(handle);
  if(!found.empty())return found;
 }
 return "";
}
