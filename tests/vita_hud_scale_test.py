from pathlib import Path
import re,subprocess,tempfile
s=Path('src/g_shared/sbarinfo.cpp').read_text()
start=s.index('inline void adjustRelCenter(');end=s.index('\nclass DSBarInfo',start)
adjust=s[start:end]
start=s.index('\tvoid DrawGraphic(');end=s.index('\tFRemapTable* GetTranslation()',start)
methods=s[start:end]
tags=sorted(set(re.findall(r'\bDTA_\w+',methods)))
code=r'''
#include <cassert>
#include <cstdio>
#include <cmath>
#include <climits>
#include <vector>
#include <type_traits>
#include "v_hudscale.h"
using BYTE=unsigned char;using fixed_t=int;
const int FRACUNIT=65536,HR_SHADOW=32768,DIM_OVERLAY=0,TAG_DONE=0;
int SCREENWIDTH=480,SCREENHEIGHT=272;
bool hud_scale=true,vid_fps=false;
double requestedScale=1;
double V_HudScale(){return requestedScale;}
template<class T>T MIN(T a,T b){return a<b?a:b;}
template<class T>T MAX(T a,T b){return a>b?a:b;}
struct FRemapTable{};
using EColorRange=int;
const int NumTextColors=32,TEXTCOLOR_ESCAPE=28,CR_UNDEFINED=-1;
EColorRange V_ParseFontColor(const BYTE*&,int,int){return 0;}
struct FTexture {
 double width=100,height=40,left=0,top=0;int LeftOffset=0;
 double GetScaledWidthDouble(){return width;}
 double GetScaledHeightDouble(){return height;}
 double GetScaledLeftOffsetDouble(){return left;}
 double GetScaledTopOffsetDouble(){return top;}
};
struct FFont {
 FTexture glyph;FRemapTable remap;
 FFont(){glyph.width=8;glyph.height=12;}
 FRemapTable*GetColorTranslation(int){return &remap;}
 int GetSpaceWidth(){return 4;}
 int GetCharWidth(unsigned char){return 8;}
 FTexture*GetChar(unsigned char,int *width){*width=8;return &glyph;}
};
struct SBarInfoCoordinate {
 double value;bool center;
 SBarInfoCoordinate(double v,bool c=false):value(v),center(c){}
 double operator*()const{return value;}
 bool RelCenter()const{return center;}
 void operator+=(int x){value+=x;}
};
struct SBarInfoCommand {enum{CENTER=1,CENTER_BOTTOM=2};};
struct SBarInfo {enum{ALIGN_CENTER,ALIGN_RIGHT};};
struct Script {
 int resW=1024,resH=768,height=0,cleanX=1,cleanY=1;
 char spacingCharacter=0;int spacingAlignment=0;
};
struct Record {double x,y,width=0,height=0,left=0,top=0,right=INT_MAX,bottom=INT_MAX;};
std::vector<Record> records;
'''
code+='\nenum {'+','.join(t+'='+str(i+1) for i,t in enumerate(tags))+'};\n'
code+=r'''
struct Canvas {
 Record current;
 void ReadTags(int){}
 template<class T,class...Rest>void ReadTags(int tag,T value,Rest...rest) {
  if constexpr(std::is_arithmetic<T>::value) {
   switch(tag) {
    case DTA_DestWidthF:current.width=double(value);break;
    case DTA_DestHeightF:current.height=double(value);break;
    case DTA_ClipLeft:current.left=double(value);break;
    case DTA_ClipTop:current.top=double(value);break;
    case DTA_ClipRight:current.right=double(value);break;
    case DTA_ClipBottom:current.bottom=double(value);break;
   }
  }
  ReadTags(rest...);
 }
 template<class...Tags>void DrawTexture(FTexture*,double x,double y,Tags...tags) {
  current=Record{x,y};ReadTags(tags...);records.push_back(current);
 }
 void VirtualToRealCoords(double &x,double &y,double &w,double &h,int vw,int vh,bool) {
  x*=double(SCREENWIDTH)/vw;y*=double(SCREENHEIGHT)/vh;
  w*=double(SCREENWIDTH)/vw;h*=double(SCREENHEIGHT)/vh;
 }
 void Clear(int x,int y,int right,int bottom,int,int){records.push_back(Record{double(x),double(y),double(right-x),double(bottom-y)});}
};
Canvas canvas;Canvas *screen=&canvas;
struct {int BlackIndex=0;}GPalette;
'''
native=Path('src/v_draw.cpp').read_text()
start=native.index('\t\tcase DTA_HUDRules:')
end=native.index('\t\tcase DTA_VirtualWidth:',start)
rules=native[start:end].replace('case DTA_HUDRules:','').rsplit('break;',1)[0]
rules=rules.replace('intval = va_arg(tags, int);','')
code+='\nstruct DrawParms {double x,y,texwidth=100,texheight=40,destwidth=100,destheight=40;};\n'
code+='\nvoid ApplyRules(DrawParms *parms,int intval) {int CleanXfac=2,CleanYfac=2,Width=SCREENWIDTH,Height=SCREENHEIGHT;const int HUD_HorizCenter=1;'+rules+'}\n'
code+=adjust
code+='\nstruct DSBarInfo {\nScript data;Script *script=&data;bool Scaled=false;int ST_X=0,ST_Y=768;\nFRemapTable*GetTranslation()const{return nullptr;}\n'+methods+'\n};\n'
code+=r'''
void Near(double a,double b){if(std::abs(a-b)>=1e-8)fprintf(stderr,"actual=%f expected=%f\n",a,b);assert(std::abs(a-b)<1e-8);}
int main() {
 DSBarInfo bar;FTexture image;FFont font;
 // A GMOTA-style lower-right element shrinks around that corner, not the center.
 for(double size:{1.0,0.75,0.5,0.25,1.5}) {
  requestedScale=size;records.clear();
  bar.DrawGraphic(&image,{-144},{-88},0,0,FRACUNIT,true);
  Near(records[0].x,480-144*size);Near(records[0].y,272-88*size);
  Near(records[0].width,100*size);Near(records[0].height,40*size);
 }
 // Relative-center coordinates retain their center anchor at any size.
 requestedScale=.5;records.clear();
 bar.DrawGraphic(&image,{0,true},{-60},0,0,FRACUNIT,true);
 Near(records[0].x,240);Near(records[0].y,242);
 // The independent multiplier works when the original stretch setting is off.
 hud_scale=false;records.clear();
 bar.DrawGraphic(&image,{10},{-60},0,0,FRACUNIT,true);
 Near(records[0].x,5);Near(records[0].y,242);Near(records[0].width,50);
 // Bars use clipping bounds; their fill still tracks the scaled frame.
 hud_scale=true;records.clear();
 bar.DrawGraphic(&image,{10},{-60},0,0,FRACUNIT,true,false,false,0,false,-1,-1,0,0,50*FRACUNIT,0);
 Near(records[0].right-records[0].x,25);
 // Glyphs and their spacing scale together at the same corner as images.
 records.clear();bar.DrawString(&font,"AB",{-144},{-88},0,0,FRACUNIT,true,0);
 assert(records.size()==2);Near(records[0].x,408.5);Near(records[0].width,4);
 Near(records[1].x-records[0].x,4);Near(records[0].y,228);
 // Native fullscreen HUD rules receive the same multiplier without changing other draw tags.
 requestedScale=.5;hud_scale=true;
 DrawParms native{-144,-88};ApplyRules(&native,0);
 Near(native.x,336);Near(native.y,184);Near(native.destwidth,100);
 hud_scale=false;native=DrawParms{-144,-88};ApplyRules(&native,0);
 Near(native.x,408);Near(native.y,228);Near(native.destwidth,50);
 native=DrawParms{0,-60};ApplyRules(&native,1);Near(native.x,240);
 // Statusbar-coordinate graphics shrink around bottom center.
 bar.Scaled=true;bar.data.resW=320;bar.data.resH=200;bar.data.height=32;bar.ST_Y=168;
 records.clear();requestedScale=1;
 bar.DrawGraphic(&image,{0},{0},0,0,FRACUNIT,false);
 Record original=records[0];records.clear();requestedScale=.5;
 bar.DrawGraphic(&image,{0},{0},0,0,FRACUNIT,false);
 Near(records[0].x,240+(original.x-240)*.5);
 Near(records[0].y,272+(original.y-272)*.5);
 Near(records[0].width,original.width*.5);
}
'''
with tempfile.TemporaryDirectory(prefix='vita-hud-test-') as d:
 cpp=Path(d)/'test.cpp';binary=Path(d)/'test'
 cpp.write_text(code)
 subprocess.run(['g++','-std=c++17','-Isrc','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(cpp),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
print('PASS: actual SBARINFO graphics, text, edge/center anchors, clipped bars, statusbar mode, native HUD rules and independent scaling')