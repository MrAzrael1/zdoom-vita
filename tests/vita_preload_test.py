"""Exercise the actual preload loops with fake assets, including budget edges."""
from pathlib import Path
import tempfile,subprocess
r=Path(__file__).resolve().parents[1]
t=(r/'src/textures/texturemanager.cpp').read_text()
a=t.index('\tif (vita_precacheeffects)',t.index('void FTextureManager::PrecacheLevel'))
b=t.index('\n#endif',a);textures=t[a:b]
s=(r/'src/s_sound.cpp').read_text()
a=s.index('\t\tif (vita_precacheeffects)',s.index('void S_PrecacheLevel'))
b=s.index('\n#endif',a);sounds=s[a:b]
code=r'''
#include <vector>
#include <cassert>
#include <cstdio>
#include <cstddef>
#include <algorithm>
using BYTE=unsigned char;
template<class T> struct Array:std::vector<T>{unsigned Size(){return this->size();}};
bool vita_precacheeffects=true;
void I_VitaTrace(const char*,...){}
struct Style {int value; bool operator==(Style b){return value==b.value;}};
enum {STYLE_Normal};
Style LegacyRenderStyles[]={{0}};
enum {MF_MISSILE=1,MF5_NOINTERACTION=2};
struct AActor{Style RenderStyle;int flags=0,flags5=0;};
#define RUNTIME_CLASS(a) 0
struct FState{unsigned sprite;};
struct FActorInfo{FState* OwnedStates;int NumOwnedStates;};
struct PClass{FActorInfo* ActorInfo;AActor* defaults;bool IsDescendantOf(int){return true;}PClass* ParentClass=nullptr;static Array<PClass*> m_Types;};
Array<PClass*> PClass::m_Types;
AActor* GetDefaultByType(PClass* type){return type->defaults;}
struct FTextureID{int index;bool isValid()const{return index>=0;}int GetIndex()const{return index;}};
struct spriteframe_t {FTextureID Texture[16];};
struct Sprite {int numframes,spriteframes;};
Array<Sprite> sprites;
Array<spriteframe_t> SpriteFrames;
struct FTexture{int w,h,calls=0;int GetWidth(){return w;}int GetHeight(){return h;}void GetPixels(){++calls;}};
Array<FTexture*> texes;
FTexture* ByIndex(int i){return texes[i];}
struct Handle{bool valid=false;bool isValid(){return valid;}};
struct sfxinfo_t {enum{NO_LINK=-1};Handle data;int link=-1;bool bRandomHeader=false,bPlayerReserve=false,bUsed=false;int lumpnum=1;unsigned frames=0;int loads=0,unloads=0;};
Array<sfxinfo_t> S_sfx;
struct Wad{int GetLumpFile(int lump){return 0;}int LumpLength(int lump){return lump==9?1024*1024+1:512;}} Wads;
struct Sound{unsigned GetSampleLength(Handle &data){for(auto &s:S_sfx)if(&s.data==&data)return s.frames;assert(false);return 0;}} renderer,*GSnd=&renderer;
sfxinfo_t* S_LoadSound(sfxinfo_t* s){s->loads++;s->data.valid=s->lumpnum!=8;return s;}
void S_UnloadSound(sfxinfo_t* s){s->unloads++;s->data.valid=false;}
size_t S_VitaPrecacheBytes(sfxinfo_t &s){return s.lumpnum==9?size_t(-1):size_t(s.frames)*4;}
void WarmTextures(){int cnt=texes.Size();BYTE hitlist[8]={1,0,0,0,0,0,0,0};
'''
code+=textures+r'''
}
void WarmSounds(){unsigned i;
'''+sounds+r'''
}
int main(){
 FTexture existing{10,10}, small{20,30}, huge{32768,32768}, edge{8192,4096}, opaque{32,32};
 texes={&existing,&small,&huge,&edge,&opaque};
 Sprite sp{1,0};sprites={sp};
 spriteframe_t frame;for(auto &id:frame.Texture)id.index=-1;
 frame.Texture[0].index=0;frame.Texture[1].index=1;frame.Texture[2].index=2;frame.Texture[3].index=3;
 SpriteFrames={frame};
 FState states[]={{0},{999}};FActorInfo ai{states,2};AActor translucent{{1}}, normal{{0}};
 PClass actor{&ai,&translucent};PClass::m_Types={&actor};
 WarmTextures();assert(existing.calls==0 && small.calls==1 && huge.calls==0 && edge.calls==0);
 // Exact budget fits; over-budget remains lazy and invalid state sprite is ignored.
 texes[1]=&edge;WarmTextures();assert(edge.calls==1);
 actor.defaults=&normal;WarmTextures();assert(edge.calls==1);
 vita_precacheeffects=false;actor.defaults=&translucent;WarmTextures();assert(edge.calls==1);
 vita_precacheeffects=true;
 // Normal-style projectiles must preload ancestor-owned sprite states.
 FState invalidState[]={{999}};FActorInfo childInfo{invalidState,1};
 PClass parent{&ai,&normal};actor.ActorInfo=&childInfo;actor.ParentClass=&parent;
 normal.flags=MF_MISSILE;WarmTextures();assert(edge.calls==2);
 normal.flags=0;actor.ParentClass=nullptr;actor.ActorInfo=&ai;
 S_sfx.resize(9);
 S_sfx[1].data.valid=true;
 S_sfx[2].link=1;
 S_sfx[3].bRandomHeader=true;
 S_sfx[4].bPlayerReserve=true;
 S_sfx[5].lumpnum=9;
 S_sfx[6].frames=16*1024*1024/4; // exact retained PCM budget
 S_sfx[7].frames=1; // unload the newly decoded overflow
 S_sfx[8].lumpnum=8; // decode failure
 WarmSounds();
 assert(S_sfx[1].data.valid && S_sfx[1].loads==0 && S_sfx[1].unloads==0);
 for(int i=2;i<=5;i++)assert(S_sfx[i].loads==0);
 assert(S_sfx[6].data.valid && S_sfx[6].bUsed);
 assert(!S_sfx[7].data.valid && S_sfx[7].loads==0 && S_sfx[7].unloads==0);
 assert(!S_sfx[8].data.valid && !S_sfx[8].bUsed);
 vita_precacheeffects=false;WarmSounds();assert(S_sfx[7].loads==0);
 puts("Vita preload tests passed: style selection, duplicates, budget edges, failures, disable");
}
'''
# Preserve aggregate assignment while using the engine-style Size adapter.
code=code.replace('unsigned Size(){','using std::vector<T>::operator=;unsigned Size(){')
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['g++','-std=c++17','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
