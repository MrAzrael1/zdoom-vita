from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
code=r'''
#include <cassert>
#include "posix/vita_texture_startup.h"
static Uint64 clockValue=1000;
extern "C" Uint64 SDL_GetPerformanceCounter(){return clockValue;}
extern "C" Uint64 SDL_GetPerformanceFrequency(){return 1000;}
FVitaTextureStartupStats VitaTextureStartup;
int main(){
 VitaTextureStartup.Reset();
 {FVitaTexturePhase phase(FVitaTextureStartupStats::Sprites);clockValue+=20;}
 assert(VitaTextureStartup.calls[FVitaTextureStartupStats::Sprites]==1);
 assert(VitaTextureStartup.phases[FVitaTextureStartupStats::Sprites]==20);
 {FVitaTextureProbe probe(7,4096);clockValue+=3;probe.Opened();clockValue+=9;probe.Accepted();}
 assert(VitaTextureStartup.probes==1 && VitaTextureStartup.accepted==1);
 assert(VitaTextureStartup.payloadBytes==4096);
 assert(VitaTextureStartup.phases[FVitaTextureStartupStats::ProbeOpen]==3);
 assert(VitaTextureStartup.phases[FVitaTextureStartupStats::ProbeIdentify]==9);
 assert(VitaTextureStartup.slowLumps[0].index==7 && VitaTextureStartup.slowLumps[0].ms==12);
 for(int i=0;i<50;++i)VitaTextureStartup.Slowest(VitaTextureStartup.slowWads,VitaTextureStartup.wadCount,i,i);
 assert(VitaTextureStartup.wadCount==8);
 for(int i=0;i<8;++i)assert(VitaTextureStartup.slowWads[i].index==49-i);
 VitaTextureStartup.active=false;
 {FVitaTextureProbe probe(9,999);probe.Opened();probe.Accepted();clockValue+=5;}
 assert(VitaTextureStartup.probes==1 && VitaTextureStartup.accepted==1);
 VitaTextureStartup.Reset();
 assert(VitaTextureStartup.probes==0 && VitaTextureStartup.wadCount==0);
}
'''
with tempfile.TemporaryDirectory(prefix='vita-texinit-test-') as folder:
 p=Path(folder);(p/'test.cpp').write_text(code)
 subprocess.run(['g++','-std=c++17','-Isrc','-I/usr/local/vitasdk/arm-vita-eabi/include/SDL2','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(p/'test.cpp'),'-o',str(p/'test')],cwd=root,check=True)
 subprocess.run([str(p/'test')],check=True)
 print('PASS: texture startup phases, nested probes, bounded slowest lists and disabled/reset behavior')
