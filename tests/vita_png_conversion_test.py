"""Compare Vita PNG conversion against the unchanged upstream conversion."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'src/textures/pngtexture.cpp').read_text()
a=s.index('\t\t\t// Convert sequentially'); b=s.index('\n#else',a); c=s.index('\n#endif',b)
new=s[a:b];old=s[b+len('\n#else'):c]
t=(r/'src/textures/texture.cpp').read_text()
a=t.index('void FTexture::FlipNonSquareBlock (');b=t.index('\nvoid FTexture::FlipNonSquareBlockRemap',a)
transpose=t[a:b].replace('FTexture::','')
base=r'''
#include <vector>
#include <cassert>
#include <cstdio>
#include <random>
using BYTE=unsigned char;
struct { BYTE RGB[32][32][32]; } RGB32k;
'''
signature='''std::vector<BYTE> NAME(std::vector<BYTE> data, int Width,int Height,int ColorType,bool HaveTrans,const BYTE *PaletteMap) {
 BYTE NonPaletteTrans[3]={17,39,91};
 BYTE *tempix=data.data(), *in=tempix;
 std::vector<BYTE> pixels(Width*Height,0xee);
 BYTE *Pixels=pixels.data(), *out=Pixels;
 int x,y,pitch,backstep;
'''
code=base+transpose+signature.replace('NAME','before')+old+'\nreturn pixels;}\n'+signature.replace('NAME','after')+new+r'''
return pixels;}
int main(){
 std::mt19937 rng(872);
 for(int r=0;r<32;r++)for(int g=0;g<32;g++)for(int b=0;b<32;b++)RGB32k.RGB[r][g][b]=rng();
 BYTE map[256];for(int i=0;i<256;i++)map[i]=rng();
 for(int type: {2,4,6})for(int w: {1,2,7,32,61,256,513})for(int h: {1,3,19,128,257})
 for(bool trans: {false,true})for(bool mapped: {false,true}){
  int stride=type==2?3:type==4?2:4;
  std::vector<BYTE> input(w*h*stride);for(auto &v:input)v=rng();
  for(int i=0;i<w*h;i++){
   if(type==2 && i%4==0){input[i*stride]=17;input[i*stride+1]=39;input[i*stride+2]=91;}
   if(type!=2 && i%5<2)input[i*stride+stride-1]=i%5==0?127:128;
  }
  assert(before(input,w,h,type,trans,mapped?map:nullptr)==after(input,w,h,type,trans,mapped?map:nullptr));
 }
 puts("Vita PNG conversion: 420 format/dimension/transparency cases match upstream");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['g++','-std=c++17','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
