"""Exercise actual world-detail orchestration, cleanup and paletted enlargement."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
inc=(root/'src/posix/vita_world_detail.inc').read_text()
inc='\n'.join(line for line in inc.splitlines() if not line.startswith('#include'))
code=r'''
#include <cassert>
#include <cstdio>
#include <vector>
#include <cstring>
#include <stdexcept>
#include <random>
#include "posix/vita_world_detail.h"
#define CUSTOM_CVAR(type,name,value,flags) int name=value;void Clamp_##name(int&self)
enum{OF_YesReallyDelete=1,OF_Fixed=2,VPERF_WORLD_SCALE};
struct FVitaPerfScope{FVitaPerfScope(int){}};
int live=0;bool validCanvas=true;
struct DSimpleCanvas {
 int width,height,pitch,locks=0,ObjectFlags=0;bool valid;std::vector<unsigned char> pixels;
 DSimpleCanvas(int w,int h):width(w),height(h),pitch(w+3),valid(validCanvas),pixels(pitch*h,99){++live;}
 ~DSimpleCanvas(){--live;}
 void Destroy(){}
 int GetWidth(){return width;}int GetHeight(){return height;}int GetPitch(){return pitch;}
 unsigned char*GetBuffer(){assert(locks>0);return pixels.data();}
 void Lock(bool=true){++locks;}void Unlock(){assert(locks>0);--locks;}
 bool IsValid(){return valid;}
};
struct Actor{};struct player_t{Actor*mo;};
struct FSoftwareRenderer {
 DSimpleCanvas *VitaWorldCanvas=nullptr;FVitaWorldScaler VitaWorldScaler;
 ~FSoftwareRenderer();void ReleaseVitaWorld();bool RenderVitaWorld(player_t*);
};
DSimpleCanvas*screen=nullptr,*RenderTarget=nullptr;
bool viewactive=true,bRenderingToCanvas=false;
int ST_Y=240,setblocks=12,viewwidth=480,viewheight=272,viewwindowx=0,viewwindowy=0;
int weaponCalls=0,worldWidth=0,worldHeight=0,worldCalls=0;
bool failRender=false;
extern bool VitaRenderingLowWorld;
struct{unsigned char BlackIndex=0;}GPalette;
void I_VitaTrace(const char*,...){}
void I_VitaPerfWorldSize(int w,int h){worldWidth=w;worldHeight=h;}
void R_SetWindow(int blocks,int w,int h,int status){
 viewwidth=blocks>=10?w:((blocks*w)/10)&~15;
 viewheight=blocks>=11?h:blocks==10?status:((blocks*status)/10)&~7;
}
void R_ExecuteSetViewSize(){
 R_SetWindow(setblocks,screen->width,screen->height,ST_Y);
 viewwindowx=(screen->width-viewwidth)/2;
 viewwindowy=viewwidth==screen->width?0:(ST_Y-viewheight)/2;
}
void R_SetupBuffer(){assert(RenderTarget==screen);}
void R_SetupFreelook(){assert(RenderTarget==screen);}
void R_DrawPlayerSprites(){
 if(VitaRenderingLowWorld)return;
 assert(RenderTarget==screen&&!bRenderingToCanvas);
 ++weaponCalls;
 screen->GetBuffer()[(viewwindowy+viewheight-1)*screen->pitch+viewwindowx+viewwidth/2]=255;
}
void R_RenderActorView(Actor*){
 ++worldCalls;
 assert(RenderTarget!=screen && VitaRenderingLowWorld && bRenderingToCanvas);
 if(failRender)throw std::runtime_error("render failure");
 for(int y=0;y<viewheight;++y)for(int x=0;x<viewwidth;++x)
  RenderTarget->GetBuffer()[y*RenderTarget->pitch+x]=(x+y)%250+1;
 R_DrawPlayerSprites(); // Suppressed until the full framebuffer is restored.
}
'''
code+=inc+r'''
int main(){
 // Verify cached enlargement against an exact nearest-neighbor reference.
 std::mt19937 rng(13);FVitaWorldScaler scaler;
 for(int n=0;n<250;++n){
  int w=1+rng()%180,h=1+rng()%120,sw=1+rng()%w,sh=1+rng()%h,sp=sw+3,dp=w+5;
  std::vector<unsigned char> src(sp*sh),dst(dp*h,99);
  for(auto&v:src)v=rng();
  scaler.Blit(dst.data(),dp,w,h,src.data(),sp,sw,sh);
  for(int y=0;y<h;++y)for(int x=0;x<dp;++x){
   unsigned char expected=x<w?src[(y*sh/h)*sp+x*sw/w]:99;
   assert(dst[y*dp+x]==expected);
  }
 }
 DSimpleCanvas display(480,272);screen=RenderTarget=&display;display.Lock();
 Actor actor;player_t player{&actor};FSoftwareRenderer renderer;
 assert(!renderer.RenderVitaWorld(&player)&&worldCalls==0&&live==1);
 for(int mode:{1,2})for(int blocks:{8,10,11,12}){
  vita_worlddetail=mode;setblocks=blocks;R_ExecuteSetViewSize();
  int targetW=viewwidth,targetH=viewheight,x0=viewwindowx,y0=viewwindowy;
  std::fill(display.pixels.begin(),display.pixels.end(),99);
  int oldWeapons=weaponCalls;assert(renderer.RenderVitaWorld(&player));
  assert(weaponCalls==oldWeapons+1);
  assert(RenderTarget==screen&&!VitaRenderingLowWorld&&!bRenderingToCanvas);
  assert(viewwidth==targetW&&viewheight==targetH&&viewwindowx==x0&&viewwindowy==y0);
  assert(display.locks==1&&renderer.VitaWorldCanvas->locks==0);
  assert(worldWidth<targetW&&worldHeight<targetH);
  for(int y=0;y<272;++y)for(int x=0;x<display.pitch;++x){
   unsigned char expected=99;
   if(x>=x0&&x<x0+targetW&&y>=y0&&y<y0+targetH)
    expected=((x-x0)*worldWidth/targetW+(y-y0)*worldHeight/targetH)%250+1;
   if(y==y0+targetH-1&&x==x0+targetW/2)expected=255;
   assert(display.pixels[y*display.pitch+x]==expected);
  }
 }
 failRender=true;bool caught=false;
 try{renderer.RenderVitaWorld(&player);}catch(std::runtime_error&){caught=true;}
 assert(caught&&RenderTarget==screen&&!VitaRenderingLowWorld&&!bRenderingToCanvas);
 assert(renderer.VitaWorldCanvas->locks==0&&display.locks==1);
 failRender=false;assert(renderer.RenderVitaWorld(&player));
 viewactive=false;assert(!renderer.RenderVitaWorld(&player));viewactive=true;
 renderer.ReleaseVitaWorld();assert(live==1);
 validCanvas=false;assert(!renderer.RenderVitaWorld(&player));assert(live==1);validCanvas=true;
 int invalid=-1;Clamp_vita_worlddetail(invalid);assert(invalid==0);
 invalid=9;Clamp_vita_worlddetail(invalid);assert(invalid==2);
 display.Unlock();
 puts("PASS: exact world enlargement, full-size weapon/UI, view blocks, modes, canvas ownership and error restoration");
}
'''
with tempfile.TemporaryDirectory(prefix='vita-world-detail-') as folder:
 p=Path(folder);(p/'test.cpp').write_text(code)
 subprocess.run(['g++','-std=c++17','-Isrc','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(p/'test.cpp'),'-o',str(p/'test')],cwd=root,check=True)
 subprocess.run([str(p/'test')],check=True)
