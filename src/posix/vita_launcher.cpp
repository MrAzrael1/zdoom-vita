// Pre-engine Vita file selector. No engine graphics or configuration required.
#include <SDL.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/ime_dialog.h>
#include <psp2/sysmodule.h>
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
bool VerboseLogging=false, LauncherOptionsReady=false;
void Scan(const std::string &root,const std::string &relative, int depth, bool wantIwad, std::vector<File> &files, bool &limited) {
 if(depth>16 || files.size()>=512){limited=true;return;}
 std::string directory=root+relative;
 SceUID handle=sceIoDopen(directory.c_str());
 if(handle<0){I_VitaTrace("[Vita] Launcher: cannot scan %s (%d)\n",directory.c_str(),handle);return;}
 SceIoDirent entry;
 for(;;) {
  if(files.size()>=512){limited=true;break;}
  memset(&entry,0,sizeof(entry));
  int result=sceIoDread(handle,&entry);
  if(result<=0)break;
  std::string name=entry.d_name;
  if(name.empty() || name[0]=='.' || name.find_first_of("\r\n")!=std::string::npos)continue;
  std::string path=relative+name;
  if(path.size()>900){limited=true;continue;}
  if(SCE_S_ISDIR(entry.d_stat.st_mode)) {
   if(!wantIwad)files.push_back({root+path+"/",false,true});
   Scan(root,path+"/",depth+1,wantIwad,files,limited);continue;
  }
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
void Load(Selection &selection, std::string *arguments=NULL,std::string *config=NULL) {
 VerboseLogging=false;
 FILE *f=fopen("ux0:data/zdoom-vita/launcher.cfg","r");
 if(!f)return;
 char line[1024];
 while(fgets(line,sizeof(line),f)) {
  size_t n=strlen(line);
  while(n && (line[n-1]=='\n'||line[n-1]=='\r'))line[--n]=0;
  if(strncmp(line,"verbose_logging=",16)==0)VerboseLogging=strcmp(line+16,"1")==0;
  else if(strncmp(line,"iwad=",5)==0)selection.iwad=line+5;
  else if(config && strncmp(line,"config=",7)==0)*config=line+7;
  else if(arguments && strncmp(line,"args=",5)==0)*arguments=line+5;
  else if(strncmp(line,"mod=",4)==0 && selection.mods.size()<512)selection.mods.push_back(line+4);
 }
 fclose(f);
}
void Save(const Selection &selection,const std::string &arguments="",const std::string &config="") {
 FILE *f=fopen("ux0:data/zdoom-vita/launcher.cfg","w");
 if(!f){I_VitaTrace("[Vita] Launcher: selection save failed\n");return;}
 fprintf(f,"iwad=%s\nargs=%s\nconfig=%s\n",selection.iwad.c_str(),arguments.c_str(),config.c_str());
 fprintf(f,"verbose_logging=%d\n",VerboseLogging?1:0);
 for(const std::string &p:selection.mods)fprintf(f,"mod=%s\n",p.c_str());
 bool okay=!ferror(f);
 if(fflush(f)!=0)okay=false;
 if(fclose(f)!=0)okay=false;
 I_VitaTrace("[Vita] Launcher: selection save %s\n",okay?"OK":"FAILED");
}

const char *ConfigRoot="ux0:data/zdoom-vita/configs/";
std::vector<std::string> Configs() {
 std::vector<std::string> names;
 SceUID dir=sceIoDopen(ConfigRoot);
 if(dir<0)return names;
 SceIoDirent entry;
 while(names.size()<512) {
  memset(&entry,0,sizeof(entry));
  if(sceIoDread(dir,&entry)<=0)break;
  std::string filename=entry.d_name;
  if(SCE_S_ISDIR(entry.d_stat.st_mode) || filename.size()<5 ||
     Lower(filename.substr(filename.size()-4))!=".txt")continue;
  std::string name=filename.substr(0,filename.size()-4);
  if(ConfigName(name)==name && !name.empty())names.push_back(filename);
 }
 sceIoDclose(dir);
 std::sort(names.begin(),names.end(),[](const std::string &a,const std::string &b){return Lower(a)<Lower(b);});
 return names;
}
bool ReadConfig(const std::string &filename,std::vector<std::string> &mods,std::string &notice,std::string *iwad=NULL) {
 FILE *f=fopen((std::string(ConfigRoot)+filename).c_str(),"r");
 if(!f){notice="CANNOT OPEN CONFIG";return false;}
 std::vector<std::string> loaded;std::string game;char line[1024];bool okay=true;
 while(fgets(line,sizeof(line),f)) {
  size_t n=strlen(line);
  if(n && line[n-1]!='\n' && !feof(f)){okay=false;break;}
  while(n && (line[n-1]=='\n'||line[n-1]=='\r'))line[--n]=0;
  if(!n || line[0]=='#')continue;
  if(strncmp(line,"iwad=",5)==0) {
   if(!game.empty() || !line[5]){okay=false;break;}
   game=Resolve(line+5,true);continue;
  }
  if(strncmp(line,"mod=",4)!=0 || loaded.size()>=9){okay=false;break;}
  loaded.push_back(Resolve(line+4,false));
 }
 if(ferror(f))okay=false;
 fclose(f);
 if(!okay){notice="INVALID CONFIG FILE";return false;}
 mods.swap(loaded);if(iwad)*iwad=game;return true;
}
bool WriteConfig(const std::string &name,const Selection &selection,std::string &notice) {
 sceIoMkdir("ux0:data/zdoom-vita/configs",0777);
 std::string path=std::string(ConfigRoot)+name+".txt";
 std::string temporary=path+".tmp",backup=path+".bak";
 SceIoStat stat;
 bool replacing=sceIoGetstat(path.c_str(),&stat)>=0;
 if(sceIoGetstat(backup.c_str(),&stat)>=0) {
  notice="CONFIG BACKUP EXISTS - CHECK CONFIGS FOLDER";return false;
 }
 FILE *f=fopen(temporary.c_str(),"w");
 if(!f){notice="CANNOT CREATE CONFIG";return false;}
 fprintf(f,"# ZDoom Vita config v2\niwad=%s\n",selection.iwad.c_str());
 for(const std::string &mod:selection.mods)fprintf(f,"mod=%s\n",DisplayPath(mod).c_str());
 bool okay=!ferror(f);
 if(fflush(f)!=0)okay=false;
 if(fclose(f)!=0)okay=false;
 if(!okay){sceIoRemove(temporary.c_str());notice="CONFIG SAVE FAILED";return false;}
 // Vita rename does not replace an existing target. Keep the previous file
 // available for rollback until the newly written config takes its place.
 if(replacing && sceIoRename(path.c_str(),backup.c_str())<0) {
  sceIoRemove(temporary.c_str());notice="CANNOT REPLACE CONFIG";return false;
 }
 if(sceIoRename(temporary.c_str(),path.c_str())<0) {
  if(replacing && sceIoRename(backup.c_str(),path.c_str())<0)
   I_VitaTrace("[Vita] Launcher: config recovery needed: %s\n",backup.c_str());
  sceIoRemove(temporary.c_str());notice="CONFIG SAVE FAILED";return false;
 }
 if(replacing)sceIoRemove(backup.c_str());
 notice=std::string(replacing?"REPLACED CONFIG: ":"SAVED CONFIG: ")+name;return true;
}
// SDL's Vita renderer presents native dialogs alongside the launcher.
struct Keyboard {
 int purpose=0;
 SceWChar16 text[513]={},initial[513]={},title[32]={};
 bool Open(int requested,const char *heading,const std::string &value,std::string &notice) {
  if(sceSysmoduleIsLoaded(SCE_SYSMODULE_IME)<0 && sceSysmoduleLoadModule(SCE_SYSMODULE_IME)<0) {
   notice="CANNOT LOAD VITA KEYBOARD";return false;
  }
  memset(text,0,sizeof(text));memset(initial,0,sizeof(initial));memset(title,0,sizeof(title));
  for(size_t i=0;i<value.size()&&i<512;++i)initial[i]=(unsigned char)value[i];
  for(size_t i=0;heading[i]&&i<31;++i)title[i]=(unsigned char)heading[i];
  SceImeDialogParam param;sceImeDialogParamInit(&param);
  param.type=SCE_IME_TYPE_DEFAULT;param.option=SCE_IME_OPTION_NO_ASSISTANCE;
  param.dialogMode=SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
  param.textBoxMode=SCE_IME_DIALOG_TEXTBOX_MODE_WITH_CLEAR;
  param.title=title;param.initialText=initial;param.inputTextBuffer=text;
  param.maxTextLength=requested==1?64:512;
  SDL_StopTextInput();
  if(sceImeDialogInit(&param)<0){notice="CANNOT OPEN VITA KEYBOARD";return false;}
  purpose=requested;return true;
 }
 bool Finish(int &requested,std::string &value,bool &accepted) {
  if(!purpose || sceImeDialogGetStatus()!=SCE_COMMON_DIALOG_STATUS_FINISHED)return false;
  SceImeDialogResult result={};
  int status=sceImeDialogGetResult(&result);
  sceImeDialogTerm();requested=purpose;purpose=0;
  accepted=status>=0 && result.result==0 && result.button==SCE_IME_DIALOG_BUTTON_ENTER;
  value.clear();
  for(size_t i=0;i<512&&text[i];++i)value+=text[i]<128?char(text[i]):char(127);
  return true;
 }
};

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

void Text(SDL_Renderer *r,int x,int y,const std::string &text,int scale,int limit=74,double size=1.0) {
 if(FontTexture) {
  int right=std::min(928,x+limit*6*scale),cell=scale*12;
  int total=0;
  for(unsigned char c:text) {
   c=c<128?(unsigned char)std::toupper(c):'?';
   if(c<32 || c>127)c='?';
   total+=(FontWidth[c-32]*cell+31)/32+2;
  }
  if(total>right-x)cell=std::max(12,cell*(right-x)/total);
  cell=std::max(1,int(cell*size+0.5));
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

namespace {
SDL_Window *StartupWindow=NULL;
SDL_Renderer *StartupRenderer=NULL;
SDL_Texture *StartupImage=NULL;
int StartupImageWidth=0,StartupImageHeight=0;
void (*StartupSink)(const char*)=NULL;
std::vector<std::string> StartupLines;
Uint32 StartupLastDraw=0;
bool StartupHasDrawn=false;
void DrawStartupConsole(bool force) {
 if(!StartupRenderer)return;
 Uint32 now=SDL_GetTicks();
 if(!force && StartupHasDrawn && now-StartupLastDraw<100)return;
 StartupLastDraw=now;StartupHasDrawn=true;
 Color(StartupRenderer,0,0,0);SDL_RenderClear(StartupRenderer);
 Color(StartupRenderer,220,220,220);
 // Use the readable fixed-width fallback, independent of the launcher font.
 SDL_Texture *font=FontTexture;FontTexture=NULL;
 for(size_t i=0;i<StartupLines.size();++i)
  Text(StartupRenderer,24,20+int(i)*22,StartupLines[i],2,76);
 FontTexture=font;
 SDL_RenderPresent(StartupRenderer);
}
}
void I_VitaStartupEnd() {
 StartupSink=NULL;
 if(StartupImage)SDL_DestroyTexture(StartupImage);
 StartupImage=NULL;StartupImageWidth=StartupImageHeight=0;
 if(StartupRenderer)SDL_DestroyRenderer(StartupRenderer);
 if(StartupWindow)SDL_DestroyWindow(StartupWindow);
 StartupRenderer=NULL;StartupWindow=NULL;
 StartupLines.clear();StartupHasDrawn=false;
}
void I_VitaStartupBegin() {
 I_VitaStartupEnd();
 if(SDL_InitSubSystem(SDL_INIT_VIDEO)<0)return;
 StartupWindow=SDL_CreateWindow("ZDoom Vita",SDL_WINDOWPOS_UNDEFINED,
  SDL_WINDOWPOS_UNDEFINED,960,544,SDL_WINDOW_FULLSCREEN);
 if(StartupWindow)StartupRenderer=SDL_CreateRenderer(StartupWindow,-1,SDL_RENDERER_ACCELERATED);
 if(!StartupRenderer){I_VitaStartupEnd();return;}
 SDL_RenderSetLogicalSize(StartupRenderer,960,544);
 StartupLines.push_back("");
}
void I_VitaStartupSetSink(void (*sink)(const char*)) {
 StartupSink=sink;
}
void I_VitaStartupReplay(void (*sink)(const char*)) {
 if(!sink)return;
 for(size_t i=0;i<StartupLines.size();++i){
  sink(StartupLines[i].c_str());
  if(i+1<StartupLines.size())sink("\n");
 }
}
void I_VitaStartupImage(const unsigned int *pixels,int width,int height,bool fullWidth) {
 if(!StartupRenderer||!pixels||width<=0||height<=0||width>640||height>480)return;
 if(!StartupImage||StartupImageWidth!=width||StartupImageHeight!=height){
  if(StartupImage)SDL_DestroyTexture(StartupImage);
  StartupImage=SDL_CreateTexture(StartupRenderer,SDL_PIXELFORMAT_ARGB8888,
   SDL_TEXTUREACCESS_STREAMING,width,height);
  StartupImageWidth=width;StartupImageHeight=height;
 }
 if(!StartupImage||SDL_UpdateTexture(StartupImage,NULL,pixels,width*4)<0)return;
 Color(StartupRenderer,0,0,0);SDL_RenderClear(StartupRenderer);
 int w=fullWidth?960:std::min(960,544*width/height);
 int h=fullWidth?544:w*height/width;
 SDL_Rect dest={(960-w)/2,(544-h)/2,w,h};
 SDL_RenderCopy(StartupRenderer,StartupImage,NULL,&dest);
 SDL_RenderPresent(StartupRenderer);
}
void I_VitaStartupPrint(const char *text) {
 if(!StartupRenderer || !text)return;
 if(StartupSink){StartupSink(text);return;}
 for(const unsigned char *p=(const unsigned char*)text;*p;++p) {
  if(*p=='\r'){StartupLines.back().clear();continue;}
  if(*p=='\n' || StartupLines.back().size()>=76) {
   StartupLines.push_back("");
   if(StartupLines.size()>23)StartupLines.erase(StartupLines.begin());
   if(*p=='\n')continue;
  }
  if(*p=='\t')StartupLines.back()+=" ";
  else if(*p>=32)StartupLines.back()+=char(*p<127?*p:'?');
 }
 DrawStartupConsole(false);
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
 std::string arguments,activeConfig;
 Load(selection);
 LauncherOptionsReady=true;
 // Only the last IWAD is retained. Mod/folder selections, active configs and
 // extra arguments belong to this launcher session, including nested files.
 selection.mods.clear();
 std::vector<std::string> extraArgs;
 if(!ParseArguments(arguments,extraArgs))arguments.clear();
 std::vector<std::string> configs=Configs();
 selection.iwad=Resolve(selection.iwad,true);
 const std::vector<File> allFiles=files;
 HideRedundantShareware(files,selection.iwad);
 selection.Validate(files);
 Save(selection); // Sanitize launcher.cfg even when the user cancels.
 std::vector<int> lists[2];
 for(size_t i=0;i<files.size();++i)if(files[i].iwad)lists[0].push_back(int(i));
 std::string browser=PwadRoot;
 auto refreshBrowser=[&]() {
  lists[1].clear();
  for(size_t i=0;i<files.size();++i)if(!files[i].iwad) {
   std::string path=files[i].path;
   if(files[i].directory)path.pop_back();
   if(path.substr(0,path.find_last_of('/')+1)==browser)lists[1].push_back(int(i));
  }
  std::stable_sort(lists[1].begin(),lists[1].end(),[&](int a,int b) {
   if(files[a].directory!=files[b].directory)return files[a].directory;
   return Lower(files[a].path)<Lower(files[b].path);
  });
 };
 refreshBrowser();
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
 int tab=0,focus[4]={0,0,0,0},top[4]={0,0,0,0};
 Keyboard keyboard;
 Selection preview;
 std::string previewName,previewError;
 bool previewLegacy=false;
 Uint32 cooldown=0,folderPressTime=0;
 std::string pressedFolder;
 bool folderHeld=false;
 for(size_t i=0;i<lists[0].size();++i)if(files[lists[0][i]].path==selection.iwad)focus[0]=int(i);
 bool running=true,launch=false;
 std::string notice;
 auto count=[&](){return tab==3?1:tab==2?int(configs.size()+1):int(lists[tab].size());};
 auto choose=[&]() {
  if(tab==3) {
   VerboseLogging=!VerboseLogging;
   Save(selection,arguments,activeConfig);
   notice.clear();
   return;
  }
  if(tab==2) {
   if(focus[2]==0) {
    if(selection.iwad.empty()){notice="SELECT AN IWAD BEFORE SAVING";return;}
    if(ValidateConfig(selection.mods,files,notice))keyboard.Open(1,"Set a name","",notice);
   } else {
    if(activeConfig==configs[focus[2]-1]) {
     selection.mods.clear();activeConfig.clear();notice="CONFIG DISABLED";return;
    }
    std::vector<std::string> loaded;std::string game;
    if(ReadConfig(configs[focus[2]-1],loaded,notice,&game) && ValidateConfig(loaded,files,notice)) {
     if(!game.empty()) {
      bool found=false;for(const File &file:allFiles)if(file.iwad && file.path==game){found=true;break;}
      if(!found){notice="MISSING IWAD: "+DisplayPath(game);return;}
      selection.iwad=game;
      for(size_t i=0;i<lists[0].size();++i)if(files[lists[0][i]].path==game)focus[0]=int(i);
     }
     selection.mods.swap(loaded);activeConfig=configs[focus[2]-1];notice="LOADED CONFIG: "+activeConfig;
    }
   }
   return;
  }
  if(lists[tab].empty())return;
  const File &f=files[lists[tab][focus[tab]]];
  if(tab==0){selection.iwad=f.path;activeConfig.clear();notice.clear();}
  else if(f.directory){browser=f.path;refreshBrowser();focus[1]=top[1]=0;notice.clear();}
  else {selection.Toggle(f.path);activeConfig.clear();notice.clear();}
 };
 while(running) {
  int requested=0;std::string entered;bool accepted=false;
  if(keyboard.Finish(requested,entered,accepted)) {
   SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT);
   cooldown=SDL_GetTicks()+250;
   if(accepted && requested==1) {
    std::string name=ConfigName(entered);
    if(name.empty())notice="USE LETTERS, NUMBERS, SPACES OR UNDERSCORES";
    else if(WriteConfig(name,selection,notice)) {
     configs=Configs();activeConfig=name+".txt";previewName.clear();
     auto it=std::find(configs.begin(),configs.end(),activeConfig);
     focus[2]=int(it-configs.begin())+1;
    }
   } else if(accepted && requested==2) {
    std::vector<std::string> parsed;
    if(ParseArguments(entered,parsed)){arguments=entered;extraArgs.swap(parsed);notice.clear();}
    else notice="INVALID ARGUMENTS - CHECK QUOTES AND ASCII TEXT";
   }
  }
  SDL_Event e;
  while(!keyboard.purpose && (Sint32)(SDL_GetTicks()-cooldown)>=0 && SDL_PollEvent(&e)) {
   if(e.type==SDL_QUIT){running=false;break;}
   int action=-1;
   if(e.type==SDL_JOYBUTTONUP && e.jbutton.button==2 && !pressedFolder.empty()) {
    if(!folderHeld && tab==1) {
     browser=pressedFolder;refreshBrowser();focus[1]=top[1]=0;notice.clear();
    }
    pressedFolder.clear();folderHeld=false;continue;
   }
   if(e.type==SDL_JOYBUTTONDOWN) {
    int b=e.jbutton.button;
    if(b==8)action=0;else if(b==6)action=1;else if(b==7||b==4)action=2;
    else if(b==9||b==5)action=3;
    else if(b==2) {
     if(tab==1 && !lists[1].empty() && files[lists[1][focus[1]]].directory) {
      pressedFolder=files[lists[1][focus[1]]].path;folderPressTime=SDL_GetTicks();folderHeld=false;
     } else action=4;
    }
    else if(b==11)action=5;else if(b==3)action=9;else if(b==0)action=6;else if(b==1)action=7;else if(b==10)action=8;
   } else if(e.type==SDL_KEYDOWN) {
    switch(e.key.keysym.sym) {
     case SDLK_UP:action=0;break;case SDLK_DOWN:action=1;break;
     case SDLK_LEFT:action=2;break;case SDLK_RIGHT:case SDLK_TAB:action=3;break;
     case SDLK_DELETE:action=9;break;case SDLK_F2:action=8;break;case SDLK_RETURN:action=4;break;case SDLK_F5:action=5;break;case SDLK_ESCAPE:action=7;break;
    }
   } else if(e.type==SDL_FINGERDOWN && e.tfinger.touchId==1) {
    int x=int(e.tfinger.x*960),y=int(e.tfinger.y*544);
    if(y>=70&&y<105)tab=std::min(3,std::max(0,(x-32)/228));
    else if(y>=RowY&&y<RowY+Rows*RowHeight && (tab!=2 || (x>=32 && x<432))) {
     int i=top[tab]+(y-RowY)/RowHeight;
     if(i<count()){focus[tab]=i;choose();}
    } else if(y>=478&&y<513 && x>=680)action=5;
   }
   if(action>=0){pressedFolder.clear();folderHeld=false;}
   if(action==0&&count())focus[tab]=(focus[tab]+count()-1)%count();
   if(action==1&&count())focus[tab]=(focus[tab]+1)%count();
   if(action==2)tab=(tab+3)%4;
   if(action==3)tab=(tab+1)%4;
   if(action==4)choose();
   if(action==5&&!selection.iwad.empty()) {
    if(BaseName(selection.iwad)=="hexdd.wad" && RequiredBase(files,selection.iwad).empty())
     notice="HEXDD.WAD REQUIRES HEXEN.WAD IN THE IWAD FOLDER";
    else {launch=true;running=false;}
   }
   if(action==6){selection.mods.clear();activeConfig.clear();notice.clear();}
   if(action==9 && tab==2 && focus[2]>0) {
    std::string filename=configs[focus[2]-1];
    if(sceIoRemove((std::string(ConfigRoot)+filename).c_str())<0)notice="CANNOT DELETE CONFIG";
    else {
     if(activeConfig==filename)activeConfig.clear();
     configs=Configs();previewName.clear();focus[2]=std::min(focus[2],int(configs.size()));
     notice="DELETED CONFIG: "+filename.substr(0,filename.size()-4);
    }
   }
   if(action==8)keyboard.Open(2,"Launch arguments",arguments,notice);
   if(action==7) {
    if(tab==1 && browser!=PwadRoot) {
     std::string path=browser.substr(0,browser.size()-1);
     browser=path.substr(0,path.find_last_of('/')+1);
     refreshBrowser();focus[1]=top[1]=0;notice.clear();
    } else if(tab==2 && !activeConfig.empty()) {
     selection.mods.clear();activeConfig.clear();notice="CONFIG DISABLED";
    } else if(tab)tab=0;else running=false;
   }
   if(keyboard.purpose)break;
  }
  if(!pressedFolder.empty() && !folderHeld && SDL_GetTicks()-folderPressTime>=550) {
   selection.Toggle(pressedFolder);activeConfig.clear();folderHeld=true;
   notice=selection.Order(pressedFolder)?"FOLDER SELECTED FOR LOADING":"FOLDER REMOVED FROM LOAD LIST";
  }
  if(focus[tab]<top[tab])top[tab]=focus[tab];
  if(focus[tab]>=top[tab]+Rows)top[tab]=focus[tab]-Rows+1;
  Color(renderer,16,19,27);SDL_RenderClear(renderer);
  Color(renderer,230,232,238);Text(renderer,32,22,"ZDOOM VITA 0.12.0",3);
  Color(renderer,139,151,175);Text(renderer,32,51,"SELECT YOUR GAME AND OPTIONAL MODS",2);
  const char *tabs[]={"IWADS","MODS","CONFIGS","OPTIONS"};
  for(int t=0;t<4;++t) {
   Color(renderer,tab==t?70:34,tab==t?83:40,tab==t?116:55);Rect(renderer,32+t*228,70,212,32);
   Color(renderer,240,240,245);Text(renderer,48+t*228,80,tabs[t],2,22);
  }
  for(int row=0;row<Rows;++row) {
   int i=top[tab]+row;
   if(i>=count())break;
   if(i==focus[tab]){Color(renderer,46,54,73);Rect(renderer,32,RowY+row*RowHeight,tab==2?400:896,30);}
   std::string label;
   if(tab==3)label=std::string("Verbose Logging: ")+(VerboseLogging?"On":"Off");
   else if(tab==2)label=i==0?"[+] SAVE CURRENT CONFIG":(configs[i-1]==activeConfig?"[X] ":"[ ] ")+configs[i-1].substr(0,configs[i-1].size()-4);
   else {
    const File &f=files[lists[tab][i]];
    int order=selection.Order(f.path);
    if(tab==0)label=(selection.iwad==f.path?"[X] ":"[ ] ")+DisplayPath(f.path);
   else label=(order?"["+std::to_string(order)+"] ":"[ ] ")+(f.directory?std::string("[DIR] "):std::string(""))+f.path.substr(browser.size());
   }
   Color(renderer,230,232,238);Text(renderer,44,RowY+row*RowHeight+8,label,2,tab==2?32:72);
  }
  if(tab==3) {
   Color(renderer,139,151,175);
   Text(renderer,44,174,"Enable advanced logging for debugging.",2);
   Text(renderer,44,206,"Logging may reduce performance",2);
  }
  if(tab==2) {
   Color(renderer,34,40,55);Rect(renderer,448,RowY,480,Rows*RowHeight);
   Color(renderer,70,83,116);Rect(renderer,438,RowY,2,Rows*RowHeight);
   Selection detail=selection;std::string error;bool legacy=false;
   if(focus[2]>0) {
    const std::string &filename=configs[focus[2]-1];
    if(previewName!=filename) {
     previewName=filename;preview=Selection();previewError.clear();
     if(ReadConfig(filename,preview.mods,previewError,&preview.iwad))
      previewLegacy=preview.iwad.empty();
     else previewLegacy=false;
    }
    detail=preview;error=previewError;legacy=previewLegacy;
    if(legacy)detail.iwad=selection.iwad;
   }
   Color(renderer,230,232,238);
   Text(renderer,464,124,focus[2]?"CONFIG CONTENTS":"CURRENT SELECTION",2,38,0.85);
   if(!error.empty())Text(renderer,464,158,error,2,38,0.85);
   else {
    Text(renderer,464,152,(legacy?"IWAD (CURRENT): ":"IWAD: ")+
     (detail.iwad.empty()?std::string("NONE"):DisplayPath(detail.iwad)),2,38,0.85);
    Text(renderer,464,180,"MODS - LOAD ORDER",2,38,0.85);
    if(detail.mods.empty())Text(renderer,464,208,"NO MODS SELECTED",2,38,0.85);
    for(size_t i=0;i<detail.mods.size() && i<9;++i) {
     std::string path=DisplayPath(detail.mods[i]),kind;
     if(!path.empty() && path.back()=='/')kind="FOLDER";
     else {
      size_t dot=path.find_last_of('.');
      kind=dot==std::string::npos?"MOD":path.substr(dot+1);
     }
     std::string label=std::to_string(i+1)+". "+kind+" "+path;
     if(label.size()>48) {
      std::string trimmed=path;
      if(!trimmed.empty() && trimmed.back()=='/')trimmed.pop_back();
      trimmed=trimmed.substr(trimmed.find_last_of('/')+1);
      label=std::to_string(i+1)+". "+kind+" "+trimmed;
     }
     Text(renderer,464,208+int(i)*24,label,2,38,0.85);
    }
   }
  }
  if(tab<2 && lists[tab].empty()) {
   Color(renderer,230,232,238);Text(renderer,44,148,tab?"NO MOD FILES FOUND":"NO IWAD FILES FOUND",2);
   Text(renderer,44,184,tab?"PUT MODS IN UX0:DATA/ZDOOM-VITA/PWAD":"PUT GAMES IN UX0:DATA/ZDOOM-VITA/IWAD",2);
  }
  Color(renderer,139,151,175);
  Text(renderer,32,448,!notice.empty()?notice.c_str():limited?"SCAN LIMIT REACHED - FIRST 512 ENTRIES, 16 SUBFOLDER LEVELS":tab==3?"OPTIONS: X TOGGLES THE SELECTED OPTION":tab==2?"CONFIGS: X LOADS OR DISABLES A SAVED CONFIG":tab?("MODS: "+browser).c_str():"IWADS: UX0:data/zdoom-vita/iwad",2);
  Text(renderer,32,475,"IWAD: "+(selection.iwad.empty()?std::string("NONE"):DisplayPath(selection.iwad)),2,52);
  Text(renderer,32,495,tab==3?"X TOGGLE - O BACK":tab==2?"SQUARE DELETES SELECTED CONFIG":tab==1?"X SELECT O BACK - HOLD X LOAD FOLDER":"MODS: "+std::to_string(selection.mods.size())+" - TRIANGLE CLEARS",2,52,tab==1?0.95:1.0);
  Color(renderer,selection.iwad.empty()?50:55,selection.iwad.empty()?50:104,selection.iwad.empty()?50:78);
  Rect(renderer,680,478,248,35);Color(renderer,245,245,245);Text(renderer,708,488,"START GAME",2);
  Color(renderer,139,151,175);Text(renderer,32,526,"DPAD BROWSE  L/R CHANGE TAB  SELECT ARGS  START LAUNCH",2,74,0.95);
  SDL_RenderPresent(renderer);
  SDL_Delay(8);
 }
 if(keyboard.purpose){sceImeDialogAbort();sceImeDialogTerm();}
 if(joystick)SDL_JoystickClose(joystick);
 if(FontTexture){SDL_DestroyTexture(FontTexture);FontTexture=NULL;}
 SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);
 SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT);
 if(!launch)return false;
 Save(selection,arguments,activeConfig);
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
 std::string baseIwad=RequiredBase(files,selection.iwad);
 if(!baseIwad.empty()) {
  args->AppendArg("-vita-base-iwad");args->AppendArg(baseIwad.c_str());
  I_VitaTrace("[Vita] Launcher: required Hexen base=%s\n",baseIwad.c_str());
 }
 for(const std::string &arg:extraArgs)args->AppendArg(arg.c_str());
 I_VitaTrace("[Vita] Launcher: IWAD=%s mods=%d\n",selection.iwad.c_str(),int(selection.mods.size()));
 for(const std::string &mod:selection.mods)I_VitaTrace("[Vita] Launcher: mod=%s\n",mod.c_str());
 return true;
}
// Apply after engine settings load so an old zdoom.ini cannot undo the toggle.
bool I_VitaLauncherProfile(bool &enabled) {
 if(!LauncherOptionsReady)return false;
 enabled=VerboseLogging;
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
