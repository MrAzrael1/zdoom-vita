"""Exercise the real Vita timing accumulator with a deterministic fake clock."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'src/posix/sdl/sdlvideo.cpp').read_text()
a=s.index('static Uint64 VitaPerfTotals')
b=s.index('\n#endif',a)
enum=(root/'src/posix/vita_perf.h').read_text().split('enum EVitaPerfStage ')[1].split(';')[0]
code=r'''
#include <cassert>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
using Uint64 = unsigned long long;
static Uint64 clockTick=1;
static bool enabled=true;
bool I_VitaPerfEnabled(){return enabled;}
Uint64 SDL_GetPerformanceCounter(){return clockTick;}
Uint64 SDL_GetPerformanceFrequency(){return 1000;}
struct Screen { int GetWidth(){return 480;} int GetHeight(){return 272;} } scr, *screen=&scr;
std::vector<std::string> logs;
void I_VitaTrace(const char* fmt,...){char b[2048];va_list ap;va_start(ap,fmt);vsnprintf(b,sizeof(b),fmt,ap);va_end(ap);logs.emplace_back(b);}
'''
code+='enum EVitaPerfStage '+enum+';\n'+s[a:b]+r'''
int main(){
 // A brief expensive explosion followed by a cheaper reporting frame.
 clockTick=11; I_VitaPerfAdd(VPERF_THINKERS,1);
 clockTick=31; I_VitaPerfAdd(VPERF_SPRITE_DRAW,11);
 I_VitaPerfAdd(VPERF_PNG_LOAD,21); I_VitaPerfAdd(VPERF_SOUND_LOAD,29);
 I_VitaPerfSprites(80);
 clockTick=51; I_VitaPerfFrame(1);
 assert(VitaPerfSlowFrames==1 && VitaPerfSpritesWorst==80);
 assert(VitaPerfWorst[VPERF_THINKERS]==10);
 clockTick=4991; I_VitaPerfAdd(VPERF_THINKERS,4990);
 clockTick=4993; I_VitaPerfAdd(VPERF_SPRITE_DRAW,4991);
 I_VitaPerfSprites(3);
 clockTick=5001; I_VitaPerfFrame(4990);
 assert(logs.size()==5);
 assert(logs[3].find("worst-frame PNG=10.00 sound=2.00")!=std::string::npos);
 assert(logs[1].find("peak=80")!=std::string::npos);
 assert(logs[1].find("=1/2")!=std::string::npos);
 assert(logs[2].find("thinkers=10.00")!=std::string::npos);
 assert(logs[2].find("sprites=20.00")!=std::string::npos);
 assert(logs[2].find("passes=80")!=std::string::npos);
 // Starting another window must not retain the explosion's stage totals.
 clockTick=5010; I_VitaPerfFrame(5002);
 assert(VitaPerfWorst[VPERF_THINKERS]==0);
 assert(VitaPerfSpritesCurrent==0);
 enabled=false; I_VitaPerfSprites(99); I_VitaPerfFrame(0);
 assert(VitaPerfFrames==0 && VitaPerfWindowStart==0);
 enabled=true; clockTick=6001; I_VitaPerfFrame(6000);
 assert(VitaPerfMaxFrame==1 && VitaPerfSpritesWorst==0);
 puts("Vita performance accumulator tests passed");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['g++','-std=c++17','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
