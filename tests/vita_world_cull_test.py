"""Exercise actual experimental world-limit eligibility, state and clipped fill."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
bsp=(root/'src/r_bsp.cpp').read_text();segs=(root/'src/r_segs.cpp').read_text()
guard=bsp[bsp.index('struct FVitaFarWallGuard'):];guard=guard[:guard.index('\n#endif')]
elig=bsp[bsp.index(' if(vita_worldculling && !fake3D'):];elig=elig[:elig.index(' if(I_VitaPerfEnabled())')]
clip=segs[segs.index('\t// clip wall to the floor and ceiling',segs.index('void R_RenderSegLoop')):]
clip=clip[:clip.index('\t// kg3D - fake planes clipping')]
cap=segs[segs.index(' if(VitaFarWall) {',segs.index('void R_RenderSegLoop')):]
cap=cap[:cap.index('\n#endif')]
code=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include "posix/vita_distance_cull.h"
struct Count{int n=0;int Size()const{return n;}};
struct Extra{struct{Count ffloors;}XFloor;};
struct sector_t{sector_t*heightsec=nullptr;Extra*e=nullptr;};
sector_t *backsector=nullptr,*frontsector=nullptr;
bool VitaFarWall=false,vita_worldculling=true;
int fake3D=0,MirrorFlags=0,CurrentSkybox=0;void*ActiveWallMirror=nullptr;
int viewx=0,viewy=0,vita_culldistance=2048;
enum{WALLF_POLYOBJ=64};
struct Vertex{int32_t x,y;};struct Line{int special=0;};struct Side{int Flags=0;};
struct Seg{Vertex*v1,*v2;Line*linedef;Side*sidedef;sector_t*backsector;};
static int32_t Fixed(int n){return int32_t(int64_t(n)*65536);}
GUARD
bool Eligible(Seg*line){
 FVitaFarWallGuard vitaBoundary;backsector=line->backsector;
ELIG
 return VitaFarWall;
}
typedef uint8_t BYTE;
short walltop[8],wallbottom[8],ceilingclip[8],floorclip[8];
struct Plane{short top[8],bottom[8];}floor_,ceiling_,*floorplane=&floor_,*ceilingplane=&ceiling_;
struct FakeFloor{short floorclip[8],ceilingclip[8];}*fakeFloor=nullptr;
bool markfloor=true,markceiling=true,viewactive=true;int viewheight=8;
BYTE framebuffer[8*13],*dc_destorg=framebuffer;int ylookup[8];
struct{BYTE BlackIndex=0;}GPalette;
template<class A,class B> int MIN(A a,B b){return a<b?a:b;}
template<class A,class B> int MAX(A a,B b){return a>b?a:b;}
void clearbufshort(short*p,int n,int value){for(int i=0;i<n;++i)p[i]=short(value);}
int normalDraws=0;
void RenderBand(int x1,int x2){
 int x;
CLIP
CAP
 ++normalDraws;
}
int main(){
 sector_t front,back,alternate;frontsector=&front;
 Vertex a{Fixed(4000),Fixed(-100)},b{Fixed(4000),Fixed(100)};
 Line line;Side side;Seg seg{&a,&b,&line,&side,&back};
 assert(Eligible(&seg)&&!VitaFarWall&&backsector==&back);
 vita_worldculling=false;assert(!Eligible(&seg));vita_worldculling=true;
 line.special=1;assert(!Eligible(&seg));line.special=0;
 side.Flags=WALLF_POLYOBJ;assert(!Eligible(&seg));side.Flags=0;
 fake3D=1;assert(!Eligible(&seg));fake3D=0;
 MirrorFlags=1;assert(!Eligible(&seg));MirrorFlags=0;
 CurrentSkybox=1;assert(!Eligible(&seg));CurrentSkybox=0;
 ActiveWallMirror=&front;assert(!Eligible(&seg));ActiveWallMirror=nullptr;
 front.heightsec=&back;assert(!Eligible(&seg));front.heightsec=nullptr;
 Extra floors;floors.XFloor.ffloors.n=1;back.e=&floors;assert(!Eligible(&seg));back.e=nullptr;
 a={Fixed(-4000),0};b={Fixed(4000),0};assert(!Eligible(&seg));
 // Nested closures and exceptions must restore both transient renderer fields.
 backsector=&back;
 try {FVitaFarWallGuard outer;outer.Close();assert(!backsector&&VitaFarWall);
  {FVitaFarWallGuard inner;assert(!VitaFarWall);backsector=&alternate;inner.Close();}
  assert(backsector==&alternate&&VitaFarWall);throw 1;
 }catch(int){}
 assert(backsector==&back&&!VitaFarWall);
 for(int y=0;y<8;++y)ylookup[y]=y*13;
 for(int x=0;x<8;++x){walltop[x]=4;wallbottom[x]=6;ceilingclip[x]=1;floorclip[x]=7;}
 memset(framebuffer,99,sizeof(framebuffer));VitaFarWall=true;RenderBand(2,5);
 assert(normalDraws==0);
 for(int y=0;y<8;++y)for(int x=0;x<13;++x)
  assert(framebuffer[y*13+x]==((x>=2&&x<5&&y>=4&&y<6)?0:99));
 for(int x=2;x<5;++x){
  assert(ceilingplane->top[x]==1&&ceilingplane->bottom[x]==4);
  assert(floorplane->top[x]==6&&floorplane->bottom[x]==7);
  assert(ceilingclip[x]==8&&floorclip[x]==-1);
 }
 // Clip to framebuffer bounds before filling a wall that crosses the screen.
 walltop[0]=-20;wallbottom[0]=100;ceilingclip[0]=0;floorclip[0]=8;
 RenderBand(0,1);for(int y=0;y<8;++y)assert(framebuffer[y*13]==0);
 VitaFarWall=false;RenderBand(6,7);assert(normalDraws==1);
}
'''
code=code.replace('GUARD',guard).replace('ELIG',elig).replace('CLIP',clip).replace('CAP',cap)
assert 'CVAR(Bool, vita_worldculling, false, CVAR_ARCHIVE)' in bsp
assert 'if (!backsector' not in elig
with tempfile.TemporaryDirectory(prefix='vita-world-limit-') as folder:
 p=Path(folder);(p/'test.cpp').write_text(code)
 subprocess.run(['g++','-std=c++17','-Isrc','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(p/'test.cpp'),'-o',str(p/'test')],cwd=root,check=True)
 subprocess.run([str(p/'test')],check=True)
 print('PASS: world eligibility/bypasses, nested state restoration, clipped boundary fill and retained plane extents')
