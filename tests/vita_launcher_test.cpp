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
static Uint32 ticks;
static SceWChar16 *keyboardBuffer;
static std::string keyboardAnswer,keyboardHeading;
static bool keyboardCancel=false;
static int failConfigRename=0;
static std::vector<unsigned char> pixels(960*544*3);
static unsigned char red,green,blue;
static std::map<int,DIR*> dirs;
static int nextDir=1;
static std::vector<Uint32> atlas;
static int atlasWidth=512,atlasHeight=192;
static bool rawTexture=false;
extern "C" {
void *sceClibMemset(void *p,int c,SceSize n){return memset(p,c,n);}

SceInt32 sceSysmoduleIsLoaded(SceSysmoduleModuleId){return 0;}
SceInt32 sceSysmoduleLoadModule(SceSysmoduleModuleId){return 0;}
SceInt32 sceImeDialogInit(const SceImeDialogParam *param){
 keyboardBuffer=param->inputTextBuffer;keyboardHeading.clear();
 for(size_t i=0;param->title[i];++i)keyboardHeading+=char(param->title[i]);
 for(size_t i=0;i<keyboardAnswer.size()&&i<param->maxTextLength;++i)keyboardBuffer[i]=(unsigned char)keyboardAnswer[i];
 return 0;
}
SceCommonDialogStatus sceImeDialogGetStatus(){return SCE_COMMON_DIALOG_STATUS_FINISHED;}
SceInt32 sceImeDialogGetResult(SceImeDialogResult *result){result->result=0;result->button=keyboardCancel?SCE_IME_DIALOG_BUTTON_CLOSE:SCE_IME_DIALOG_BUTTON_ENTER;return 0;}
SceInt32 sceImeDialogTerm(){return 0;}
SceInt32 sceImeDialogAbort(){return 0;}
int sceIoRename(const char *from,const char *to){
 if(failConfigRename && std::string(to).size()>4 && std::string(to).substr(strlen(to)-4)==".txt"){--failConfigRename;return -1;}
 if(std::filesystem::exists(to))return -1;
 std::error_code error;std::filesystem::rename(from,to,error);return error?-1:0;
}
int sceIoRemove(const char *path){std::error_code error;bool removed=std::filesystem::remove(path,error);return removed&&!error?0:-1;}
int sceIoMkdir(const char *path,SceMode){std::error_code error;std::filesystem::create_directory(path,error);return error?-1:0;}
int sceIoGetstat(const char *path,SceIoStat *stat){if(!std::filesystem::exists(path))return -1;memset(stat,0,sizeof(*stat));return 0;}
SDL_Texture* SDL_CreateTexture(SDL_Renderer*,Uint32,int access,int w,int h){
 atlasWidth=w;atlasHeight=h;rawTexture=access==SDL_TEXTUREACCESS_STREAMING;
 atlas.resize(w*h);return (SDL_Texture*)1;
}
void SDL_DestroyTexture(SDL_Texture*){atlas.clear();}
int SDL_UpdateTexture(SDL_Texture*,const SDL_Rect*,const void*p,int){memcpy(atlas.data(),p,atlas.size()*4);return 0;}
int SDL_SetTextureBlendMode(SDL_Texture*,SDL_BlendMode){return 0;}
int SDL_SetTextureColorMod(SDL_Texture*,Uint8,Uint8,Uint8){return 0;}
int SDL_RenderCopy(SDL_Renderer*,SDL_Texture*,const SDL_Rect*s,const SDL_Rect*d) {
 for(int y=0;y<d->h;++y)for(int x=0;x<d->w;++x) {
  int px=d->x+x,py=d->y+y;if(px<0||py<0||px>=960||py>=544)continue;
  SDL_Rect full={0,0,atlasWidth,atlasHeight};if(!s)s=&full;
  Uint32 texel=atlas[(s->y+y*s->h/d->h)*atlasWidth+s->x+x*s->w/d->w];
  unsigned a=texel>>24;
  size_t i=(py*960+px)*3;unsigned color[3]={200,32,24};
  if(rawTexture){color[0]=(texel>>16)&255;color[1]=(texel>>8)&255;color[2]=texel&255;}
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
int SDL_PollEvent(SDL_Event*e){if(eventAt==events.size())return 0;*e=events[eventAt++];if(e->type==SDL_USEREVENT){ticks+=e->user.code;return 0;}return 1;}
void SDL_FlushEvents(Uint32,Uint32){}
Uint32 SDL_GetTicks(){return ticks;}
void SDL_Delay(Uint32 n){ticks+=n;}
void SDL_StopTextInput(){}
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
 assert(catalog.size()==5&&!limited&&dirs.empty());
 DArgs args;Reset();Key(SDLK_RIGHT);Key(SDLK_RETURN);Key(SDLK_RETURN);Key(SDLK_DOWN);Key(SDLK_RETURN);Key(SDLK_F5);
 assert(I_VitaLauncher(&args));
 std::vector<std::string> expected={"-iwad","ux0:data/zdoom-vita/iwad/doom2.wad","-file",
  "ux0:data/zdoom-vita/pwad/mods/a map.wad","ux0:data/zdoom-vita/pwad/mods/b.pk3"};
 assert(args.arguments==expected&&dirs.empty());
 std::ofstream image("launcher-preview.ppm",std::ios::binary);
 image<<"P6\n960 544\n255\n";image.write((const char*)pixels.data(),pixels.size());image.close();
 DArgs repeated;Reset();Key(SDLK_F5);assert(I_VitaLauncher(&repeated));assert((repeated.arguments==std::vector<std::string>{"-iwad",expected[1]}));
 DArgs touched;Reset();
 SDL_Event touch={};touch.type=SDL_FINGERDOWN;touch.tfinger.touchId=2;
 touch.tfinger.x=.2f;touch.tfinger.y=164.f/544;events.push_back(touch);
 Key(SDLK_F5);assert(I_VitaLauncher(&touched)&&touched.arguments==repeated.arguments); // Rear ignored.
 DArgs front;Reset();touch.tfinger.touchId=1;events.push_back(touch);
 touch.tfinger.x=.8f;touch.tfinger.y=495.f/544;events.push_back(touch);
 assert(I_VitaLauncher(&front)&&front.arguments[1]=="ux0:data/zdoom-vita/iwad/heretic.wad");
 DArgs cancel;Reset();Key(SDLK_ESCAPE);assert(!I_VitaLauncher(&cancel)&&cancel.arguments.empty());
 VitaLauncher::Selection selected;Load(selected);assert(selected.mods.empty());
 selected.mods={expected[3],expected[4]};
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
 DArgs strifeArgs;Reset();Key(SDLK_RIGHT);Key(SDLK_DOWN);Key(SDLK_RETURN);Key(SDLK_UP);Key(SDLK_RETURN);Key(SDLK_DOWN);Key(SDLK_DOWN);Key(SDLK_RETURN);Key(SDLK_F5);assert(I_VitaLauncher(&strifeArgs));
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
 // Deathkings retains its own IWAD identity and passes the required Hexen path.
 std::filesystem::create_directories("ux0:data/zdoom-vita/iwad/expansions");
 Write("ux0:data/zdoom-vita/iwad/expansions/HEXDD.WAD","IWAD");
 Write("ux0:data/zdoom-vita/iwad/HeXeN.WaD","IWAD");
 VitaLauncher::Selection deathkings;deathkings.iwad="ux0:data/zdoom-vita/iwad/expansions/HEXDD.WAD";
 deathkings.mods={"ux0:data/zdoom-vita/pwad/mods/b.pk3"};Save(deathkings);
 DArgs dkArgs;Reset();Key(SDLK_RIGHT);Key(SDLK_RETURN);Key(SDLK_DOWN);Key(SDLK_RETURN);Key(SDLK_F5);assert(I_VitaLauncher(&dkArgs));
 assert((dkArgs.arguments==std::vector<std::string>{"-iwad",deathkings.iwad,"-file",
 "ux0:data/zdoom-vita/pwad/mods/b.pk3","-vita-base-iwad","ux0:data/zdoom-vita/iwad/HeXeN.WaD"}));
 std::vector<VitaLauncher::File> dkCatalog;limited=false;Scan(IwadRoot,"",0,true,dkCatalog,limited);
 bool hexenVisible=false;for(const auto &f:dkCatalog)if(f.path=="ux0:data/zdoom-vita/iwad/HeXeN.WaD")hexenVisible=true;
 assert(hexenVisible);
 dkCatalog.push_back({"ux0:data/zdoom-vita/iwad/expansions/hexen.wad",true});
 assert(VitaLauncher::RequiredBase(dkCatalog,deathkings.iwad)=="ux0:data/zdoom-vita/iwad/expansions/hexen.wad");
 assert(VitaLauncher::RequiredBase(dkCatalog,"app0:/iwad/DOOM1.WAD").empty());
 std::filesystem::remove("ux0:data/zdoom-vita/iwad/HeXeN.WaD");
 DArgs noBase;Reset();Key(SDLK_F5);Key(SDLK_ESCAPE);assert(!I_VitaLauncher(&noBase)&&noBase.arguments.empty());
 std::filesystem::remove_all("ux0:data/zdoom-vita/iwad/expansions");
 // Restore the prior shareware selection for the following regression checks.
 VitaLauncher::Selection shareware;shareware.iwad="app0:/iwad/DOOM1.WAD";Save(shareware);
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
 assert(migrated.arguments.size()==2);
 for(const char *name:{"DOOM1.WAD","freedoom1.wad","freedoom2.wad"})
  std::filesystem::remove(std::string("app0:/iwad/")+name);
 std::filesystem::remove("ux0:data/zdoom-vita/iwad/not-a-game.wad");
 std::filesystem::remove("ux0:data/zdoom-vita/iwad/VoIcEs.WaD");
 assert(VitaLauncher::Resolve("ux0:data/zdoom-vita/doom2.wad",true)=="ux0:data/zdoom-vita/iwad/doom2.wad");
 assert(VitaLauncher::Resolve("ux0:data/zdoom-vita/mods/a map.wad",false)=="ux0:data/zdoom-vita/pwad/mods/a map.wad");
 std::filesystem::remove("ux0:data/zdoom-vita/iwad/doom2.wad");

 // Square no longer launches, and deletes only a selected config.
 DArgs squareNoLaunch;Reset();
 SDL_Event square={};square.type=SDL_JOYBUTTONDOWN;square.jbutton.button=3;events.push_back(square);
 Key(SDLK_ESCAPE);assert(!I_VitaLauncher(&squareNoLaunch)&&squareNoLaunch.arguments.empty());
 // Saved configs preserve order and accept multiple names and replacement.
 Write("ux0:data/zdoom-vita/iwad/doom2.wad","IWAD");
 VitaLauncher::Selection configSelection;configSelection.iwad="ux0:data/zdoom-vita/iwad/doom2.wad";
 configSelection.mods={"ux0:data/zdoom-vita/pwad/mods/b.pk3","ux0:data/zdoom-vita/pwad/mods/a map.wad"};
 Save(configSelection);
 std::filesystem::remove_all("ux0:data/zdoom-vita/configs");
 keyboardAnswer="My! List_12";keyboardCancel=false;
 DArgs saved;Reset();Key(SDLK_RIGHT);Key(SDLK_RETURN);Key(SDLK_DOWN);Key(SDLK_RETURN);Key(SDLK_UP);Key(SDLK_RETURN);Key(SDLK_RIGHT);Key(SDLK_RETURN);Key(SDLK_F5);
 assert(I_VitaLauncher(&saved));assert(keyboardHeading=="Set a name");
 std::vector<std::string> stored;std::string message;
 assert(ReadConfig("My List_12.txt",stored,message)&&stored==configSelection.mods);
 VitaLauncher::Selection replacement=configSelection;
 replacement.iwad="app0:/iwad/freedoom1.wad";replacement.mods.resize(1);
 assert(WriteConfig("My List_12",replacement,message));
 std::string replacementGame;
 assert(ReadConfig("My List_12.txt",stored,message,&replacementGame));
 assert(stored==replacement.mods&&replacementGame==replacement.iwad);
 assert(!std::filesystem::exists("ux0:data/zdoom-vita/configs/My List_12.txt.bak"));
 failConfigRename=1;
 assert(!WriteConfig("My List_12",configSelection,message));
 assert(ReadConfig("My List_12.txt",stored,message,&replacementGame));
 assert(stored==replacement.mods&&replacementGame==replacement.iwad);
 assert(WriteConfig("My List_12",configSelection,message));
 assert(WriteConfig("Another",configSelection,message)&&Configs().size()==2);
 VitaLauncher::Selection blank;blank.iwad=configSelection.iwad;Save(blank);
 DArgs loaded;Reset();Key(SDLK_RIGHT);Key(SDLK_RIGHT);Key(SDLK_DOWN);Key(SDLK_DOWN);Key(SDLK_RETURN);Key(SDLK_F5);
 assert(I_VitaLauncher(&loaded)&&loaded.arguments==configSelection.Arguments());
 std::ofstream configImage("configs-preview.ppm",std::ios::binary);
 configImage<<"P6\n960 544\n255\n";configImage.write((const char*)pixels.data(),pixels.size());configImage.close();
 // Touching config details must not activate the corresponding list row.
 DArgs detailsTouch;Reset();Key(SDLK_RIGHT);Key(SDLK_RIGHT);
 SDL_Event paneTouch={};paneTouch.type=SDL_FINGERDOWN;paneTouch.tfinger.touchId=1;
 paneTouch.tfinger.x=.8f;paneTouch.tfinger.y=164.f/544;events.push_back(paneTouch);Key(SDLK_F5);
 assert(I_VitaLauncher(&detailsTouch)&&(detailsTouch.arguments==std::vector<std::string>{"-iwad",configSelection.iwad}));

 keyboardAnswer="-skill 4 -file \"a file.pk3\"";
 DArgs manual;Reset();Key(SDLK_F2);Key(SDLK_F5);assert(I_VitaLauncher(&manual));
 assert(keyboardHeading=="Launch arguments");
 assert(manual.arguments.back()=="a file.pk3");
 DArgs persisted;Reset();Key(SDLK_F5);assert(I_VitaLauncher(&persisted)&&(persisted.arguments==std::vector<std::string>{"-iwad",configSelection.iwad}));
 keyboardAnswer="ignored";keyboardCancel=true;
 DArgs cancelledKeyboard;Reset();Key(SDLK_F2);Key(SDLK_F5);
 assert(I_VitaLauncher(&cancelledKeyboard)&&cancelledKeyboard.arguments==persisted.arguments);
 keyboardCancel=false;
 std::vector<std::string> parsed;
 assert(VitaLauncher::ParseArguments("-warp 01 +name 'Test Player'",parsed)&&parsed.back()=="Test Player");
 assert(!VitaLauncher::ParseArguments("-file \"unfinished",parsed));
 assert(VitaLauncher::ConfigName("!?").empty());
 assert(VitaLauncher::ConfigName("   My Mod List_1!   ")=="My Mod List_1");
 assert(VitaLauncher::ConfigName("   ").empty());
 // New configs store the selected IWAD, while legacy v1 files remain readable.
 std::string savedGame;
 assert(ReadConfig("My List_12.txt",stored,message,&savedGame)&&savedGame==configSelection.iwad);
 Write("ux0:data/zdoom-vita/iwad/heretic.wad","IWAD");
 VitaLauncher::Selection heretic=configSelection;heretic.iwad="ux0:data/zdoom-vita/iwad/heretic.wad";
 assert(WriteConfig("Heretic",heretic,message));
 Save(configSelection);
 DArgs gameSwitch;Reset();Key(SDLK_RIGHT);Key(SDLK_RIGHT);Key(SDLK_DOWN);Key(SDLK_DOWN);Key(SDLK_RETURN);Key(SDLK_F5);
 assert(I_VitaLauncher(&gameSwitch)&&gameSwitch.arguments==heretic.Arguments());
 // Config activation is explicit after restart; selecting it twice disables it.
 DArgs disable;Reset();Key(SDLK_RIGHT);Key(SDLK_RIGHT);Key(SDLK_DOWN);Key(SDLK_DOWN);Key(SDLK_RETURN);Key(SDLK_RETURN);Key(SDLK_F5);
 assert(I_VitaLauncher(&disable));
 assert((disable.arguments==std::vector<std::string>{"-iwad",heretic.iwad}));
 std::filesystem::remove("ux0:data/zdoom-vita/configs/Heretic.txt");
 Write("ux0:data/zdoom-vita/configs/Legacy.txt","# ZDoom Vita modlist v1\nmod=mods/b.pk3\n");
 savedGame="unchanged";assert(ReadConfig("Legacy.txt",stored,message,&savedGame)&&savedGame.empty()&&stored.size()==1);
 std::filesystem::remove("ux0:data/zdoom-vita/configs/Legacy.txt");
 Save(configSelection,"-skill 4 -file \"a file.pk3\""); // Restore expected extra arguments for later assertions.
 // Deleting a config preserves selected mods and the other saved config.
 DArgs deleted;Reset();Key(SDLK_RIGHT);Key(SDLK_RIGHT);Key(SDLK_DOWN);
 events.push_back(square);Key(SDLK_F5);assert(I_VitaLauncher(&deleted));
 assert(Configs().size()==1 && Configs()[0]=="My List_12.txt");
 assert((deleted.arguments==std::vector<std::string>{"-iwad",configSelection.iwad}));
 DArgs saveButtonProtected;Reset();Key(SDLK_RIGHT);Key(SDLK_RIGHT);
 events.push_back(square);Key(SDLK_F5);assert(I_VitaLauncher(&saveButtonProtected)&&Configs().size()==1);
 std::vector<VitaLauncher::File> limits;
 std::vector<std::string> mods;
 for(int i=0;i<9;++i){std::string path="test"+std::to_string(i)+".pk3";limits.push_back({path,false});mods.push_back(path);}
 assert(!VitaLauncher::ValidateConfig(mods,limits,message));
 mods.pop_back();assert(VitaLauncher::ValidateConfig(mods,limits,message));
 limits.push_back({"one.deh",false});limits.push_back({"two.bex",false});
 mods.push_back("one.deh");assert(VitaLauncher::ValidateConfig(mods,limits,message));
 mods.push_back("two.bex");assert(!VitaLauncher::ValidateConfig(mods,limits,message));
 mods={"missing.pk3"};assert(!VitaLauncher::ValidateConfig(mods,limits,message));
 // Tap Cross opens a folder, and Circle climbs back to the parent.
 std::filesystem::create_directories("ux0:data/zdoom-vita/pwad/mods/nested");
 Write("ux0:data/zdoom-vita/pwad/mods/nested/inside.pk3","zip");
 VitaLauncher::Selection browserBlank;browserBlank.iwad=configSelection.iwad;Save(browserBlank);
 SDL_Event crossDown={};crossDown.type=SDL_JOYBUTTONDOWN;crossDown.jbutton.button=2;
 SDL_Event crossUp=crossDown;crossUp.type=SDL_JOYBUTTONUP;
 DArgs browse;Reset();Key(SDLK_RIGHT);events.push_back(crossDown);events.push_back(crossUp);
 Key(SDLK_RETURN);Key(SDLK_RETURN);Key(SDLK_ESCAPE);Key(SDLK_F5);
 assert(I_VitaLauncher(&browse));
 assert(browse.arguments.back()=="ux0:data/zdoom-vita/pwad/mods/nested/inside.pk3");
 DArgs nestedRestart;Reset();Key(SDLK_F5);assert(I_VitaLauncher(&nestedRestart));
 assert((nestedRestart.arguments==std::vector<std::string>{"-iwad",browserBlank.iwad}));
 // Startup also clears stale state on cancel, before the game is ever launched.
 VitaLauncher::Selection stale=browserBlank;
 stale.mods={"ux0:data/zdoom-vita/pwad/mods/nested/inside.pk3"};
 Save(stale,"-file mods/nested/inside.pk3","My List_12.txt");
 DArgs sanitizedCancel;Reset();Key(SDLK_ESCAPE);assert(!I_VitaLauncher(&sanitizedCancel));
 VitaLauncher::Selection clean;std::string cleanArgs,cleanConfig;
 Load(clean,&cleanArgs,&cleanConfig);
 assert(clean.mods.empty()&&cleanArgs.empty()&&cleanConfig.empty()&&clean.iwad==browserBlank.iwad);

 // Hold Cross selects the folder itself once; releasing does not traverse.
 Save(browserBlank);DArgs folder;Reset();Key(SDLK_RIGHT);events.push_back(crossDown);
 SDL_Event idle={};idle.type=SDL_USEREVENT;idle.user.code=600;events.push_back(idle);
 events.push_back(crossUp);Key(SDLK_F5);
 assert(I_VitaLauncher(&folder));
 assert((folder.arguments==std::vector<std::string>{"-iwad",browserBlank.iwad,"-file","ux0:data/zdoom-vita/pwad/mods/"}));
 VitaLauncher::Selection folderSelection;Load(folderSelection);
 std::vector<VitaLauncher::File> folderCatalog;limited=false;Scan(PwadRoot,"",0,false,folderCatalog,limited);
 assert(VitaLauncher::ValidateConfig(folderSelection.mods,folderCatalog,message));
 assert(WriteConfig("Folder Config",folderSelection,message));
 savedGame.clear();assert(ReadConfig("Folder Config.txt",stored,message,&savedGame));
 assert(stored==folderSelection.mods && savedGame==folderSelection.iwad);
 // A new launch clears folder selections and even previously saved -file arguments.
 Save(folderSelection,"-file mods/nested/inside.pk3","Folder Config.txt");
 DArgs cleanFolder;Reset();Key(SDLK_F5);assert(I_VitaLauncher(&cleanFolder));
 assert((cleanFolder.arguments==std::vector<std::string>{"-iwad",browserBlank.iwad}));
 // Holding twice in the same session removes the folder rather than opening it.
 DArgs unselectFolder;Reset();Key(SDLK_RIGHT);events.push_back(crossDown);events.push_back(idle);
 events.push_back(crossUp);events.push_back(crossDown);events.push_back(idle);events.push_back(crossUp);Key(SDLK_F5);assert(I_VitaLauncher(&unselectFolder));
 assert((unselectFolder.arguments==std::vector<std::string>{"-iwad",browserBlank.iwad}));
 std::filesystem::remove_all("ux0:data/zdoom-vita/pwad/mods/nested");
 std::filesystem::remove("ux0:data/zdoom-vita/configs/Folder Config.txt");
 // Options: default off, fourth-tab navigation, saved toggles and front touch.
 std::filesystem::remove("ux0:data/zdoom-vita/launcher.cfg");
 DArgs defaultOptions;Reset();Key(SDLK_F5);assert(I_VitaLauncher(&defaultOptions));
 bool profile=true;assert(I_VitaLauncherProfile(profile) && !profile);
 DArgs onOptions;Reset();Key(SDLK_LEFT);Key(SDLK_RETURN);Key(SDLK_F5);
 assert(I_VitaLauncher(&onOptions));
 assert(I_VitaLauncherProfile(profile) && profile);
 DArgs savedOptions;Reset();Key(SDLK_F5);assert(I_VitaLauncher(&savedOptions));
 assert(I_VitaLauncherProfile(profile) && profile);
 DArgs offOptions;Reset();
 SDL_Event optionsTouch={};optionsTouch.type=SDL_FINGERDOWN;optionsTouch.tfinger.touchId=1;
 optionsTouch.tfinger.x=800.f/960;optionsTouch.tfinger.y=85.f/544;
 events.push_back(optionsTouch);
 optionsTouch.tfinger.x=160.f/960;optionsTouch.tfinger.y=130.f/544;
 events.push_back(optionsTouch);Key(SDLK_F5);assert(I_VitaLauncher(&offOptions));
 assert(I_VitaLauncherProfile(profile) && !profile);
 DArgs savedOff;Reset();Key(SDLK_RIGHT);Key(SDLK_RIGHT);Key(SDLK_RIGHT);
 Key(SDLK_F5);assert(I_VitaLauncher(&savedOff));
 assert(I_VitaLauncherProfile(profile) && !profile);
 // Startup console joins fragmented engine output, wraps and bounds history.
 I_VitaStartupBegin();
 I_VitaStartupPrint("W_Init: Init ");
 I_VitaStartupPrint("WADfiles.\nLoadActors: Load actor definitions.");
 assert(StartupLines.size()==2);
 assert(StartupLines[0]=="W_Init: Init WADfiles.");
 assert(StartupLines[1]=="LoadActors: Load actor definitions.");
 I_VitaStartupPrint("\rP_Init: Init Playloop state.");
 assert(StartupLines.back()=="P_Init: Init Playloop state.");
 I_VitaStartupPrint(("\n"+std::string(160,'A')).c_str());
 assert(StartupLines[StartupLines.size()-3].size()==76);
 assert(StartupLines[StartupLines.size()-2].size()==76);
 assert(StartupLines.back().size()==8);
 for(int i=0;i<40;++i)I_VitaStartupPrint("\nmessage");
 assert(StartupLines.size()==23);
 assert(StartupLines.back()=="message");
 std::vector<unsigned int> startupImage(320*200,0xff00ff00);
 I_VitaStartupImage(startupImage.data(),320,200);
 assert(pixels[(272*960+480)*3+1]==255); // centered image
 assert(pixels[(272*960)*3+1]==0); // black aspect-ratio border
 I_VitaStartupImage(startupImage.data(),320,200,true);
 assert(pixels[(272*960)*3+1]==255); // full-width console reaches screen edge
 I_VitaStartupEnd();
 assert(!StartupRenderer && !StartupWindow && StartupLines.empty());
 I_VitaStartupPrint("Ignored after engine graphics take over");
 assert(StartupLines.empty());
 I_VitaStartupEnd(); // Idempotent cleanup.
 puts("PASS: startup console fragments, wrap, scroll and graphics handoff");
 puts("PASS: Options tab, default-off logging, persisted toggles and touch");
 puts("PASS: IWAD-aware configs, disable, clean startup selections, folder tap/hold and nested navigation");
 puts("PASS: configs, native keyboard, argument parsing, limits and launcher regressions");
 puts("PASS: native scan, ordered mods, space-containing paths, saved launch, cancel and missing IWAD");
}