// Vita adaptation of src/win32/st_start.cpp startup content/animations.
// Original copyright 2006-2007 Randy Heit; same BSD terms as st_start.cpp.
#include <SDL.h>
#include <algorithm>
#include <vector>
#include "gi.h"
#include "d_main.h"
#include "w_wad.h"
#include "s_sound.h"
#include "i_sound.h"
#include "m_argv.h"
#include "posix/vita_launcher.h"
#include "posix/vita_startup_pixels.h"

class FVitaStartupScreen : public FTTYStartupScreen {
 VitaStartup::Bitmap image;
 std::vector<unsigned char> font,notch,netnotch,pics[7];
 std::vector<std::string> lines;
 int style=FStartupInfo::DoomStartup,statusY=7,statusX=1,netPosition=0;
 bool musicStarted=false,musicAttempted=false,graphical=false;
 Uint32 lastDraw=0;
 bool drawn=false;
 static FVitaStartupScreen *active;
 static bool Lump(const char *name,size_t size,std::vector<unsigned char>&out){
  int n=strchr(name,'.')?Wads.CheckNumForFullName(name):Wads.CheckNumForName(name);
  if(n<0||size_t(Wads.LumpLength(n))!=size)return false;
  out.resize(size);Wads.ReadLump(n,&out[0]);return true;
 }
 void Draw(bool force=false){
  if(!force&&drawn&&SDL_GetTicks()-lastDraw<100)return;
  drawn=true;lastDraw=SDL_GetTicks();
  I_VitaStartupImage(&image.pixels[0],image.width,image.height,style==FStartupInfo::DoomStartup);
 }
 void Pump(){
  static bool pumping=false;
  if(GSnd&&!pumping){pumping=true;S_UpdateMusic();pumping=false;}
 }
 void Strife(int oldLaser,int laser){
  image.Fill(60+oldLaser,156,16,16,image.palette[240]);
  image.Block(pics[4+(laser&1)],60+laser,156,16,16);
  int y=std::max(0,(laser>>1)%5-2);
  image.Fill(14,138,48,y,image.palette[240]);
  image.Block(pics[6],14,138+y,48,48);
  image.Fill(14,138+48+y,48,2-y,image.palette[240]);
  image.Block(pics[(laser>>1)&3],262,136,32,64);
 }
 void Console(){
  image.Fill(0,0,640,400,VitaStartup::Bitmap::RGB(0,0,0));
  image.Fill(0,0,640,16,0xff000000|(DoomStartupInfo.BkColor&0xffffff));
  // Draw the title with the mod-selected foreground/background colors.
  uint32_t oldfg=image.palette[15],oldbg=image.palette[0];
  image.palette[15]=0xff000000|(DoomStartupInfo.FgColor&0xffffff);
  image.palette[0]=0xff000000|(DoomStartupInfo.BkColor&0xffffff);
  image.Text(font,0,0,DoomStartupInfo.Name.GetChars(),15);
  image.palette[15]=oldfg;image.palette[0]=oldbg;
  for(size_t i=0;i<lines.size();++i)image.Text(font,0,int(i)+2,lines[i].c_str(),7);
  image.Fill(8,386,624,10,image.palette[8]);
  if(MaxPos>0)image.Fill(8,386,624*CurPos/MaxPos,10,image.palette[7]);
 }
 static void Output(const char *text){
  if(!active)return;
  active->Print(text);
 }
 void Print(const char *text){
  if(style==FStartupInfo::DoomStartup){
   for(const unsigned char*p=(const unsigned char*)text;*p;++p){
    if(*p=='\r'){lines.back().clear();continue;}
    if(*p=='\n'||lines.back().size()>=80){
     lines.push_back("");if(lines.size()>22)lines.erase(lines.begin());
     if(*p=='\n')continue;
    }
    if(*p>=32)lines.back()+=char(*p);else if(*p=='\t')lines.back()+=' ';
   }
   Console();Draw();
  }
  Pump();
 }
public:
 FVitaStartupScreen(int max):FTTYStartupScreen(max){
  Lump("vga-rom-font.16",4096,font);
  int requested=DoomStartupInfo.Type;
  if(requested==FStartupInfo::DefaultStartup){
   requested=gameinfo.gametype==GAME_Hexen?FStartupInfo::HexenStartup:
    gameinfo.gametype==GAME_Heretic?FStartupInfo::HereticStartup:
    gameinfo.gametype==GAME_Strife?FStartupInfo::StrifeStartup:FStartupInfo::DoomStartup;
  }
  if(Args->CheckParm("-nostartup"))requested=FStartupInfo::DoomStartup;
  std::vector<unsigned char> data,pal;
  if(requested==FStartupInfo::HexenStartup && Lump("STARTUP",153648,data)
    &&Lump("NOTCH",184,notch)&&Lump("NETNOTCH",32,netnotch)){
   style=requested;image.Resize(640,480);
   for(int i=0;i<16;++i){
    unsigned r=data[i*3],g=data[i*3+1],b=data[i*3+2];
    image.palette[i]=VitaStartup::Bitmap::RGB((r<<2)|(r>>4),(g<<2)|(g>>4),(b<<2)|(b>>4));
   }
   image.Planar(&data[48]);
  }else if(requested==FStartupInfo::HereticStartup && !font.empty()&&Lump("LOADING",4000,data)){
   style=requested;image.Resize(640,400);image.TextPalette();
   data[2*160+49*2]='3';
   for(int y=0;y<25;++y)for(int x=0;x<80;++x)
    image.Char(font,x,y,data[(y*80+x)*2],data[(y*80+x)*2+1]);
  }else if(requested==FStartupInfo::StrifeStartup && Lump("STARTUP0",64000,data)){
   int n=Wads.CheckNumForName("PLAYPAL");
   if(n>=0 && Wads.LumpLength(n)>=768){
    pal.resize(Wads.LumpLength(n));Wads.ReadLump(n,&pal[0]);
    style=requested;image.Resize(320,200);
    for(int i=0;i<256;++i)image.palette[i]=VitaStartup::Bitmap::RGB(pal[i*3],pal[i*3+1],pal[i*3+2]);
    image.Fill(0,0,320,200,image.palette[240]);
    std::vector<unsigned char> cropped(data.begin()+57*320,data.begin()+152*320);
    image.Block(cropped,0,41,320,95);
    const char *names[]={"STRTPA1","STRTPB1","STRTPC1","STRTPD1","STRTLZ1","STRTLZ2","STRTBOT"};
    for(int i=0;i<7;++i)Lump(names[i],i<4?2048:i<6?256:2304,pics[i]);
    Strife(0,0);
   }
  }
  if(style==FStartupInfo::DoomStartup){
   image.Resize(640,400);image.TextPalette();lines.push_back("");Console();
  }
  active=this;
  graphical=style!=FStartupInfo::DoomStartup || !font.empty();
  if(style!=FStartupInfo::DoomStartup || !font.empty()){
   I_VitaStartupSetSink(Output);
   if(style==FStartupInfo::DoomStartup)I_VitaStartupReplay(Output);
   Draw(true);
  }
 }
 ~FVitaStartupScreen(){
  I_VitaStartupSetSink(NULL);active=NULL;
  if(musicStarted)S_StopMusic(true);
  I_VitaStartupEnd();
 }
 void Progress(){
  if(CurPos<MaxPos)++CurPos;
  if(!musicAttempted&&(style==FStartupInfo::HexenStartup || DoomStartupInfo.Song.IsNotEmpty())){
   musicAttempted=true;
   const char *song=DoomStartupInfo.Song.IsNotEmpty()?DoomStartupInfo.Song.GetChars():"orb";
   musicStarted=S_ChangeMusic(song,0,true,true);
  }
  if(!graphical){Pump();return;}
  if(MaxPos>0){
   if(style==FStartupInfo::HexenStartup){
    int pos=CurPos*32/MaxPos;
    if(pos!=NotchPos){
     while(NotchPos<pos)image.Block(notch,64+16*NotchPos++,441,16,23,true);
     S_Sound(CHAN_BODY,"StartupTick",1,ATTN_NONE);
    }
   }else if(style==FStartupInfo::HereticStartup){
    int pos=CurPos*404/MaxPos;
    if(pos!=NotchPos&&!(pos&3)){
     image.Fill(112+NotchPos,224,pos-NotchPos,16,image.palette[10]);NotchPos=pos;
    }
   }else if(style==FStartupInfo::StrifeStartup){
    int pos=CurPos*184/MaxPos;
    if(pos!=NotchPos&&!(pos&1)){Strife(NotchPos,pos);NotchPos=pos;}
   }else Console();
  }
  Draw(CurPos==MaxPos);Pump();
 }
 void LoadingStatus(const char *message,int colors){
  if(style==FStartupInfo::HereticStartup && statusY<24){
   image.Text(font,17,statusY++,message,colors);Draw(true);
  }
  Pump();
 }
 void AppendStatusLine(const char *status){
  if(style==FStartupInfo::HereticStartup){
   image.Text(font,statusX,24,status,0x1f);statusX=std::min(80,statusX+int(strlen(status)));Draw(true);
  }
 }
 void NetProgress(int count){
  FTTYStartupScreen::NetProgress(count);
  if(style==FStartupInfo::HexenStartup){
   int pos=std::min(8,NetCurPos);
   if(pos>netPosition){
    while(netPosition<pos)image.Block(netnotch,288+4*netPosition++,32,4,16,true);
    S_Sound(CHAN_BODY,"misc/netnotch",1,ATTN_NONE);Draw(true);Pump();
   }
  }
 }
 void NetDone(){
  if(style==FStartupInfo::HexenStartup && DidNetInit)S_Sound(CHAN_BODY,"PickupWeapon",1,ATTN_NORM);
  FTTYStartupScreen::NetDone();Pump();
 }
};
FVitaStartupScreen *FVitaStartupScreen::active=NULL;
