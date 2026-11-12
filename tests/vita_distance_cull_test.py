"""Exercise actual Vita distance math and masked-wall cleanup control flow."""
from pathlib import Path
import subprocess,tempfile,re
root=Path(__file__).resolve().parents[1]
things=(root/'src/r_things.cpp').read_text()
segs=(root/'src/r_segs.cpp').read_text()
clamp=re.search(r'CUSTOM_CVAR\(Int, vita_culldistance, 4096, CVAR_ARCHIVE\)\s*\{([^}]+)\}',things).group(1)
head=segs[segs.index('\tcurline = ds->curline;',segs.index('void R_RenderMaskedSegRange')):]
head=head[:head.index('\tNetUpdate ();')]
cleanup=segs[segs.index('clearfog:',segs.index('void R_RenderMaskedSegRange')):]
cleanup=cleanup[:cleanup.index('\n\treturn;')+len('\n\treturn;')]
code=r'''
#include <cassert>
#include <cmath>
#include <random>
#include <cstdint>
#include <climits>
#include <cstring>
#include "posix/vita_distance_cull.h"
static int32_t Fixed(int n){return int32_t(int64_t(n)*65536);}
int Clamp(int self){
CLAMP
return self;
}
struct Vertex{int32_t x,y;};
struct Line{int flags=0;int Alpha=65536;};
struct Seg{Vertex*v1,*v2;Line*linedef;};
struct drawseg_t{
 Seg*curline;int maskedtexturecol=0,bFogBoundary=0,bFakeBoundary=0;
 int sprtopclip=0,bkup=8,x1=0,x2=4;
};
Seg*curline=nullptr;bool vita_distanceculling=false,vita_worldculling=false;
int vita_culldistance=4096,viewx=0,viewy=0;
bool I_VitaPerfEnabled(){return false;}
void I_VitaPerfCull(unsigned,bool){}
enum ESPSResult{Draw,DontDraw};
ESPSResult requested=Draw;
int LegacyRenderStyles[2]={0,1};
enum{ML_ADDTRANS=1,STYLE_Add=0,STYLE_Translucent=1,FRACUNIT=65536,FAKE3D_REFRESHCLIP=1};
int finishCalls=0,fakeDraws=0,normalPaths=0,fogDraws=0,midDraws=0,fake3D=0,viewheight=272;
short openings[32];
int MIN(int a,int b){return a<b?a:b;}
ESPSResult R_SetPatchStyle(int,int,int,int){return requested;}
void R_FinishSetPatchStyle(){++finishCalls;}
void R_RenderFakeWallRange(drawseg_t*,int,int){++fakeDraws;}
void clearbufshort(short*p,int count,int value){for(int i=0;i<count;++i)p[i]=value;}
void Render(drawseg_t*ds,int x1,int x2){
 bool notrelevant=false;
HEAD
 ++normalPaths;
 if(ds->bFogBoundary)++fogDraws;
 if((ds->bFakeBoundary && !(ds->bFakeBoundary&4)) || requested==DontDraw || cullMidtexture)goto clearfog;
 ++midDraws;
CLEANUP
}
int main(){
 using namespace VitaCull;
 assert(Clamp(-100)==512&&Clamp(0)==512&&Clamp(100000)==16384&&Clamp(4096)==4096);
 assert(!OutsidePoint(Fixed(512),0,0,0,0,0,512));
 assert(OutsidePoint(Fixed(513),0,0,0,0,0,512));
 assert(OutsidePoint(0,0,Fixed(600),0,0,0,512));
 assert(!OutsidePoint(Fixed(600),0,0,0,0,0,512,Fixed(100)));
 assert(!OutsideSegment(Fixed(-6000),Fixed(1000),Fixed(6000),Fixed(1000),0,0,2048));
 assert(OutsideSegment(Fixed(6000),Fixed(-100),Fixed(6000),Fixed(100),0,0,4096));
 assert(!OutsideSegment(0,0,0,0,0,0,512));
 assert(OutsidePoint(INT_MAX,INT_MAX,INT_MAX,INT_MIN,INT_MIN,INT_MIN,16384));
 std::mt19937 random(4102);
 for(int i=0;i<100000;++i){
  int32_t x=random(),y=random(),z=random(),vx=random(),vy=random(),vz=random();
  int32_t radius=random()&INT_MAX;int distance=512+random()%15873;
  long double dx=(int64_t(x)-vx)/65536.L,dy=(int64_t(y)-vy)/65536.L,dz=(int64_t(z)-vz)/65536.L;
  if(OutsidePoint(x,y,z,vx,vy,vz,distance,radius))
   assert(sqrtl(dx*dx+dy*dy+dz*dz)>distance+radius/65536.L);
  int32_t x2=random(),y2=random();
  if(OutsideSegment(x,y,x2,y2,vx,vy,distance)){
   long double ax=(int64_t(x)-vx)/65536.L,ay=(int64_t(y)-vy)/65536.L;
   long double bx=(int64_t(x2)-vx)/65536.L,by=(int64_t(y2)-vy)/65536.L;
   long double ex=bx-ax,ey=by-ay,len=ex*ex+ey*ey;
   long double t=len?-(ax*ex+ay*ey)/len:0;t=std::max(0.L,std::min(1.L,t));
   assert(hypotl(ax+t*ex,ay+t*ey)>distance);
  }
 }
 Vertex v1{Fixed(6000),Fixed(-100)},v2{Fixed(6000),Fixed(100)};
 Line line;Seg seg{&v1,&v2,&line};drawseg_t ds{&seg};
 Render(&ds,0,4);assert(normalPaths==1&&midDraws==1&&finishCalls==1);
 vita_distanceculling=true;
 memset(openings,0,sizeof(openings));
 Render(&ds,0,4);assert(normalPaths==1&&midDraws==1&&finishCalls==2);
 for(int i=0;i<4;++i)assert(openings[i]==viewheight);
 ds.bFogBoundary=1;Render(&ds,0,4);
 assert(normalPaths==2&&fogDraws==1&&midDraws==1&&finishCalls==3);
 ds.bFogBoundary=0;ds.bFakeBoundary=1;Render(&ds,0,4);
 assert(fakeDraws==1&&midDraws==1&&finishCalls==4);
 ds.bFakeBoundary=0;fake3D=FAKE3D_REFRESHCLIP;
 for(int i=0;i<4;++i)openings[8+i]=10+i;
 Render(&ds,0,4);for(int i=0;i<4;++i)assert(openings[i]==10+i);
 // No midtexture: retain the normal fog/fake processing path.
 fake3D=0;ds.maskedtexturecol=-1;Render(&ds,0,4);assert(normalPaths==4);
}
'''
code=code.replace('CLAMP',clamp).replace('HEAD',head).replace('CLEANUP',cleanup)
assert 'CVAR(Bool, vita_distanceculling, false, CVAR_ARCHIVE)' in things
assert things.index('VitaCull::OutsidePoint',things.index('void R_ProjectSprite'))<things.index('int spritenum',things.index('void R_ProjectSprite'))
with tempfile.TemporaryDirectory(prefix='vita-distance-test-') as folder:
 p=Path(folder);(p/'test.cpp').write_text(code)
 subprocess.run(['g++','-std=c++17','-DZD_VITA','-Isrc','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(p/'test.cpp'),'-o',str(p/'test')],cwd=root,check=True)
 subprocess.run([str(p/'test')],check=True)
 print('PASS: distance bounds, 100000 conservative random cases, long walls, render cleanup, fog and 3D-floor paths')
