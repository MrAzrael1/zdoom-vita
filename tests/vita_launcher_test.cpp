// Exercise actual startup UI, file scanning, persistence and argument handoff.
#include <string>
#include <vector>
#include <cassert>
#include <fstream>
#include <filesystem>
#include <map>
#include <dirent.h>
#undef st_ctime
#undef st_atime
#undef st_mtime
#define __M_ARGV_H__
#define I_VitaTrace(...) ((void)0)
class DArgs {
public: std::vector<std::string> arguments;
 void AppendArg(const char*p){arguments.push_back(p);}
};
#include "../src/posix/vita_launcher.cpp"
static std::vector<SDL_Event> events;
static size_t eventAt;
static std::vector<unsigned char> pixels(960*544*3);
static unsigned char red,green,blue;
static std::map<int,DIR*> dirs;
static int nextDir=1;
static std::vector<Uint32> atlas;
extern "C" {
SDL_Texture* SDL_CreateTexture(SDL_Renderer*,Uint32,int,int,int){atlas.resize(512*192);return (SDL_Texture*)1;}
void SDL_DestroyTexture(SDL_Texture*){atlas.clear();}
int SDL_UpdateTexture(SDL_Texture*,const SDL_Rect*,const void*p,int){memcpy(atlas.data(),p,atlas.size()*4);return 0;}
int SDL_SetTextureBlendMode(SDL_Texture*,SDL_BlendMode){return 0;}
int SDL_SetTextureColorMod(SDL_Texture*,Uint8,Uint8,Uint8){return 0;}
int SDL_RenderCopy(SDL_Renderer*,SDL_Texture*,const SDL_Rect*s,const SDL_Rect*d) {
 for(int y=0;y<d->h;++y)for(int x=0;x<d->w;++x) {
  int px=d->x+x,py=d->y+y;if(px<0||py<0||px>=960||py>=544)continue;
  unsigned a=atlas[(s->y+y*s->h/d->h)*512+s->x+x*s->w/d->w]>>24;
  size_t i=(py*960+px)*3;unsigned color[3]={200,32,24};
  for(int c=0;c<3;++c)pixels[i+c]=(color[c]*a+pixels[i+c]*(255-a))/255;
 }return 0;
}
int SDL_InitSubSystem(Uint32){return 0;}
const char*SDL_GetError(){return "test";}
SDL_Window *SDL_CreateWindow(const char*,int,int,int,int,Uint32){return (SDL_Window*)1;}
SDL_Renderer *SDL_CreateRenderer(SDL_Window*,int,Uint32){return (SDL_Renderer*)1;}
void SDL_DestroyRenderer(SDL_Renderer*){}
void SDL_DestroyWindow(SDL_Window*){}
int SDL_RenderSetLogicalSize(SDL_Renderer*,int,int){return 0;}
int SDL_NumJoysticks(){return 0;}
SDL_Joystick *SDL_JoystickOpen(int){return NULL;}
void SDL_JoystickClose(SDL_Joystick*){}
int SDL_PollEvent(SDL_Event*e){if(eventAt==events.size())return 0;*e=events[eventAt++];return 1;}
void SDL_FlushEvents(Uint32,Uint32){}
void SDL_Delay(Uint32){}
void SDL_RenderPresent(SDL_Renderer*){}
int SDL_SetRenderDrawColor(SDL_Renderer*,Uint8 r,Uint8 g,Uint8 b,Uint8){red=r;green=g;blue=b;return 0;}
int SDL_RenderFillRect(SDL_Renderer*,const SDL_Rect*rect){
 for(int y=std::max(0,rect->y);y<std::min(544,rect->y+rect->h);++y)
 for(int x=std::max(0,rect->x);x<std::min(960,rect->x+rect->w);++x){
  size_t p=(y*960+x)*3;pixels[p]=red;pixels[p+1]=green;pixels[p+2]=blue;
 }return 0;
}
int SDL_RenderClear(SDL_Renderer*r){SDL_Rect all={0,0,960,544};return SDL_RenderFillRect(r,&all);}
SceUID sceIoDopen(const char *path){DIR*d=opendir(path);if(!d)return -1;int id=nextDir++;dirs[id]=d;return id;}
int sceIoDclose(SceUID id){closedir(dirs.at(id));dirs.erase(id);return 0;}
int sceIoDread(SceUID id,SceIoDirent*dst){
 auto*entry=readdir(dirs.at(id));if(!entry)return 0;
 strncpy(dst->d_name,entry->d_name,sizeof(dst->d_name)-1);
 dst->d_stat.st_mode=entry->d_type==DT_DIR?SCE_S_IFDIR:SCE_S_IFREG;return 1;
}
}
static void Key(SDL_Keycode k){SDL_Event e={};e.type=SDL_KEYDOWN;e.key.keysym.sym=k;events.push_back(e);}
static void Reset(){events.clear();eventAt=0;}
static void Write(const std::string&p,const std::string&s){std::ofstream(p,std::ios::binary)<<s;}
int main(int argc,char**argv){
 assert(argc==2);std::filesystem::current_path(argv[1]);
 std::filesystem::create_directories("ux0:data/zdoom-vita/pwad/mods");
 std::filesystem::create_directories("ux0:data/zdoom-vita/iwad");
 std::filesystem::remove("ux0:data/zdoom-vita/launcher.cfg");
 Write("ux0:data/zdoom-vita/iwad/doom2.wad","IWAD");
 Write("ux0:data/zdoom-vita/iwad/heretic.wad","IWAD");
 Write("ux0:data/zdoom-vita/pwad/mods/a map.wad","PWAD");
 Write("ux0:data/zdoom-vita/pwad/mods/b.pk3","zip");
 Write("ux0:data/zdoom-vita/bad.wad","junk");
 Write("ux0:data/zdoom-vita/ignored.sf2","bank");
 std::vector<VitaLauncher::File> catalog;bool limited=false;Scan(IwadRoot,"",0,true,catalog,limited);Scan(PwadRoot,"",0,false,catalog,limited);
 assert(catalog.size()==4&&!limited&&dirs.empty());
 DArgs args;Reset();Key(SDLK_RIGHT);Key(SDLK_RETURN);Key(SDLK_DOWN);Key(SDLK_RETURN);Key(SDLK_F5);
 assert(I_VitaLauncher(&args));
 std::vector<std::string> expected={"-iwad","ux0:data/zdoom-vita/iwad/doom2.wad","-file",
  "ux0:data/zdoom-vita/pwad/mods/a map.wad","ux0:data/zdoom-vita/pwad/mods/b.pk3"};
 assert(args.arguments==expected&&dirs.empty());
 std::ofstream image("launcher-preview.ppm",std::ios::binary);
 image<<"P6\n960 544\n255\n";image.write((const char*)pixels.data(),pixels.size());image.close();
 DArgs repeated;Reset();Key(SDLK_F5);assert(I_VitaLauncher(&repeated));assert(repeated.arguments==expected);
 DArgs touched;Reset();
 SDL_Event touch={};touch.type=SDL_FINGERDOWN;touch.tfinger.touchId=2;
 touch.tfinger.x=.2f;touch.tfinger.y=164.f/544;events.push_back(touch);
 Key(SDLK_F5);assert(I_VitaLauncher(&touched)&&touched.arguments==expected); // Rear ignored.
 DArgs front;Reset();touch.tfinger.touchId=1;events.push_back(touch);
 touch.tfinger.x=.8f;touch.tfinger.y=495.f/544;events.push_back(touch);
 assert(I_VitaLauncher(&front)&&front.arguments[1]=="ux0:data/zdoom-vita/iwad/heretic.wad");
 DArgs cancel;Reset();Key(SDLK_ESCAPE);assert(!I_VitaLauncher(&cancel)&&cancel.arguments.empty());
 VitaLauncher::Selection selected;Load(selected);assert(selected.mods.size()==2);
 selected.mods.push_back("missing.pk3");selected.mods.push_back(selected.mods[0]);selected.Validate(catalog);
 assert(selected.mods.size()==2);
 selected.Toggle(selected.mods[0]);assert(selected.mods.size()==1);
 selected.Toggle("ux0:data/zdoom-vita/pwad/mods/a map.wad");assert(selected.Order("ux0:data/zdoom-vita/pwad/mods/a map.wad")==2);
 std::filesystem::remove("ux0:data/zdoom-vita/iwad/doom2.wad");
 std::filesystem::remove("ux0:data/zdoom-vita/iwad/heretic.wad");
 DArgs empty;Reset();Key(SDLK_F5);Key(SDLK_ESCAPE);assert(!I_VitaLauncher(&empty)&&empty.arguments.empty());
 assert(VitaLauncher::IsModExtension("patch.DEH"));
 VitaLauncher::Selection patch;patch.iwad="doom2.wad";
 patch.mods={"mods/a map.wad","mods/test.DEH","mods/test.bex"};
 assert((patch.Arguments()==std::vector<std::string>{"-iwad","ux0:data/zdoom-vita/iwad/doom2.wad",
 "-file","ux0:data/zdoom-vita/pwad/mods/a map.wad","-deh","ux0:data/zdoom-vita/pwad/mods/test.DEH","ux0:data/zdoom-vita/pwad/mods/test.bex"}));
 Write("ux0:data/zdoom-vita/VOICES.WAD","PWAD");
 assert(I_VitaCompanionFile("ux0:data/zdoom-vita/iwad/STRIFE1.WAD","voices.wad")=="ux0:data/zdoom-vita/VOICES.WAD");
 std::filesystem::create_directories("ux0:data/zdoom-vita/iwad/strife");
 Write("ux0:data/zdoom-vita/iwad/strife/Voices.wad","PWAD");
 assert(I_VitaCompanionFile("ux0:data/zdoom-vita/iwad/strife/STRIFE1.WAD","voices.wad")=="ux0:data/zdoom-vita/iwad/strife/Voices.wad");
 assert(I_VitaCompanionFile("ux0:data/zdoom-vita/iwad/strife/STRIFE1.WAD","missing.wad").empty());
 std::filesystem::remove("ux0:data/zdoom-vita/VOICES.WAD");
 std::filesystem::remove_all("ux0:data/zdoom-vita/iwad/strife");
 assert(dirs.empty());
 Write("ux0:data/zdoom-vita/iwad/STRIFE1.WAD","IWAD");
 Write("ux0:data/zdoom-vita/pwad/voices.wad","PWAD");
 Write("ux0:data/zdoom-vita/pwad/mods/test.DEH","Patch File for DeHackEd v3.0");
 std::vector<VitaLauncher::File> extra;limited=false;Scan(IwadRoot,"",0,true,extra,limited);Scan(PwadRoot,"",0,false,extra,limited);
 bool patchFound=false;for(const auto &f:extra)if(f.path=="ux0:data/zdoom-vita/pwad/mods/test.DEH")patchFound=true;
 assert(patchFound);
 VitaLauncher::Selection strife;strife.iwad="STRIFE1.WAD";
 strife.mods={"voices.wad","mods/test.DEH"};Save(strife);
 DArgs strifeArgs;Reset();Key(SDLK_F5);assert(I_VitaLauncher(&strifeArgs));
 assert((strifeArgs.arguments==std::vector<std::string>{"-iwad","ux0:data/zdoom-vita/iwad/STRIFE1.WAD","-deh","ux0:data/zdoom-vita/pwad/mods/test.DEH"}));
 std::filesystem::remove("ux0:data/zdoom-vita/iwad/STRIFE1.WAD");
 std::filesystem::remove("ux0:data/zdoom-vita/pwad/voices.wad");
 std::filesystem::remove("ux0:data/zdoom-vita/pwad/mods/test.DEH");
 // Bundled games work with no user IWADs and save their app0 paths.
 std::filesystem::create_directories("app0:/iwad");
 for(const char *name:{"DOOM1.WAD","freedoom1.wad","freedoom2.wad"})
  Write(std::string("app0:/iwad/")+name,"IWAD");
 Write("ux0:data/zdoom-vita/iwad/not-a-game.wad","PWAD");
 Write("ux0:data/zdoom-vita/iwad/VoIcEs.WaD","IWAD");
 assert(I_VitaCompanionFile("ux0:data/zdoom-vita/iwad/STRIFE1.WAD","voices.wad")=="ux0:data/zdoom-vita/iwad/VoIcEs.WaD");
 std::vector<VitaLauncher::File> games;limited=false;
 Scan("app0:/iwad/","",0,true,games,limited);Scan(IwadRoot,"",0,true,games,limited);
 assert(games.size()==3);
 std::filesystem::remove("ux0:data/zdoom-vita/launcher.cfg");
 DArgs bundled;Reset();Key(SDLK_F5);assert(I_VitaLauncher(&bundled));
 assert(bundled.arguments[1]=="app0:/iwad/DOOM1.WAD");
 DArgs bundledAgain;Reset();Key(SDLK_F5);assert(I_VitaLauncher(&bundledAgain));
 assert(bundledAgain.arguments==bundled.arguments);
 // A full Doom IWAD hides bundled shareware and replaces a saved shareware choice.
 Write("ux0:data/zdoom-vita/iwad/DoOm.WaD","IWAD");
 DArgs full;Reset();Key(SDLK_F5);assert(I_VitaLauncher(&full));
 assert(full.arguments[1]=="ux0:data/zdoom-vita/iwad/DoOm.WaD");
 std::filesystem::remove("ux0:data/zdoom-vita/iwad/DoOm.WaD");
 DArgs sharewareRestored;Reset();Key(SDLK_F5);assert(I_VitaLauncher(&sharewareRestored));
 assert(sharewareRestored.arguments[1]=="app0:/iwad/DOOM1.WAD");
 std::vector<VitaLauncher::File> mixed={{"app0:/iwad/DOOM1.WAD",true},
  {"ux0:data/zdoom-vita/iwad/sub/doom1.wad",true},{"ux0:data/zdoom-vita/pwad/doom.wad",false}};
 std::string chosen="app0:/iwad/DOOM1.WAD";
 VitaLauncher::HideRedundantShareware(mixed,chosen);assert(mixed.size()==3);
 mixed.push_back({"ux0:data/zdoom-vita/iwad/sub/DOOM.wad",true});
 VitaLauncher::HideRedundantShareware(mixed,chosen);
 assert(mixed.size()==2 && chosen=="ux0:data/zdoom-vita/iwad/sub/DOOM.wad");
 // Old relative launcher settings migrate after files move into the new folders.
 Write("ux0:data/zdoom-vita/iwad/doom2.wad","IWAD");
 Write("ux0:data/zdoom-vita/launcher.cfg","iwad=doom2.wad\nmod=mods/a map.wad\n");
 DArgs migrated;Reset();Key(SDLK_F5);assert(I_VitaLauncher(&migrated));
 assert(migrated.arguments[1]=="ux0:data/zdoom-vita/iwad/doom2.wad");
 assert(migrated.arguments.back()=="ux0:data/zdoom-vita/pwad/mods/a map.wad");
 for(const char *name:{"DOOM1.WAD","freedoom1.wad","freedoom2.wad"})
  std::filesystem::remove(std::string("app0:/iwad/")+name);
 std::filesystem::remove("ux0:data/zdoom-vita/iwad/not-a-game.wad");
 std::filesystem::remove("ux0:data/zdoom-vita/iwad/VoIcEs.WaD");
 assert(VitaLauncher::Resolve("ux0:data/zdoom-vita/doom2.wad",true)=="ux0:data/zdoom-vita/iwad/doom2.wad");
 assert(VitaLauncher::Resolve("ux0:data/zdoom-vita/mods/a map.wad",false)=="ux0:data/zdoom-vita/pwad/mods/a map.wad");
 std::filesystem::remove("ux0:data/zdoom-vita/iwad/doom2.wad");
 puts("PASS: native scan, ordered mods, space-containing paths, saved launch, cancel and missing IWAD");
}