// Startup-only texture timing. No per-resource logging or filesystem writes.
#ifndef VITA_TEXTURE_STARTUP_H
#define VITA_TEXTURE_STARTUP_H
#include <SDL.h>
#include <algorithm>
struct FVitaTextureStartupStats {
 enum Phase {Setup,Sprites,Patches,ClassicWalls,Flats,Overrides,LooseGraphics,
  Definitions,Hires,Sort,Build,Animations,Switches,Paletted,ProbeOpen,ProbeIdentify,Count};
 struct Slow {int index;double ms;};
 bool active=false;
 Uint64 begin=0;
 double phases[Count]={};
 unsigned calls[Count]={},probes=0,accepted=0;
 unsigned long long payloadBytes=0;
 Slow slowLumps[8]={},slowWads[8]={};
 int lumpCount=0,wadCount=0;
 double Milliseconds(Uint64 ticks) const {return 1000.0*double(ticks)/SDL_GetPerformanceFrequency();}
 Uint64 Clock() const {return active?SDL_GetPerformanceCounter():0;}
 void Add(Phase phase,Uint64 start) {
  if(active){phases[phase]+=Milliseconds(SDL_GetPerformanceCounter()-start);++calls[phase];}
 }
 void Slowest(Slow *list,int &count,int index,double ms) {
  int pos;
  if(count<8)pos=count++;
  else {pos=7;if(list[pos].ms>ms)return;}
  list[pos]={index,ms};
  while(pos && list[pos].ms>list[pos-1].ms){std::swap(list[pos],list[pos-1]);--pos;}
 }
 void Reset(){*this=FVitaTextureStartupStats();active=true;begin=SDL_GetPerformanceCounter();}
};
extern FVitaTextureStartupStats VitaTextureStartup;
struct FVitaTexturePhase {
 FVitaTextureStartupStats::Phase phase;Uint64 start;
 FVitaTexturePhase(FVitaTextureStartupStats::Phase p):phase(p),start(VitaTextureStartup.Clock()){}
 ~FVitaTexturePhase(){VitaTextureStartup.Add(phase,start);}
};
struct FVitaTextureProbe {
 int lump;Uint64 start,opened=0;bool enabled;
 FVitaTextureProbe(int n,unsigned length):lump(n),start(VitaTextureStartup.Clock()),enabled(VitaTextureStartup.active){
  if(enabled){++VitaTextureStartup.probes;VitaTextureStartup.payloadBytes+=length;}
 }
 void Opened(){
  if(enabled){VitaTextureStartup.Add(FVitaTextureStartupStats::ProbeOpen,start);opened=VitaTextureStartup.Clock();}
 }
 void Accepted(){if(enabled)++VitaTextureStartup.accepted;}
 ~FVitaTextureProbe(){
  if(enabled){
   if(opened)VitaTextureStartup.Add(FVitaTextureStartupStats::ProbeIdentify,opened);
   VitaTextureStartup.Slowest(VitaTextureStartup.slowLumps,VitaTextureStartup.lumpCount,lump,
    VitaTextureStartup.Milliseconds(SDL_GetPerformanceCounter()-start));
  }
 }
};
#endif
