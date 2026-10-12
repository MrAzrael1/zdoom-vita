// Safe pixel helpers for Vita's adaptations of the ZDoom DOS startup screens.
#ifndef VITA_STARTUP_PIXELS_H
#define VITA_STARTUP_PIXELS_H
#include <vector>
#include <cstdint>
#include <algorithm>
namespace VitaStartup {
struct Bitmap {
 int width=0,height=0;
 uint32_t palette[256]={};
 std::vector<uint32_t> pixels;
 void Resize(int w,int h) {width=w;height=h;pixels.assign(w*h,0xff000000);}
 static uint32_t RGB(unsigned r,unsigned g,unsigned b){return 0xff000000|(r<<16)|(g<<8)|b;}
 void Put(int x,int y,uint32_t c){if(x>=0&&y>=0&&x<width&&y<height)pixels[y*width+x]=c;}
 void Fill(int x,int y,int w,int h,uint32_t c) {
  for(int yy=std::max(y,0);yy<std::min(y+h,height);++yy)
   for(int xx=std::max(x,0);xx<std::min(x+w,width);++xx)Put(xx,yy,c);
 }
 void Block(const std::vector<unsigned char>&data,int x,int y,int w,int h,bool packed=false){
  if(data.size()!=size_t(w*h/(packed?2:1)))return;
  for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx){
   int n=yy*w+xx;unsigned c=packed?(data[n/2]>>(n%2?0:4))&15:data[n];
   Put(x+xx,y+yy,palette[c]);
  }
 }
 void Planar(const unsigned char *src){
  int plane=width*height/8;
  for(int n=0;n<width*height;++n){
   unsigned c=0;for(int p=0;p<4;++p)c|=((src[p*plane+n/8]>>(7-n%8))&1)<<p;
   pixels[n]=palette[c];
  }
 }
 void TextPalette(){
  for(int i=0;i<16;++i){
   unsigned v=(i&8)?85:0;
   palette[i]=RGB(((i&4)?170:0)+v,((i&2)?170:0)+v,((i&1)?170:0)+v);
  }
  palette[6]=RGB(170,85,0);
 }
 void Char(const std::vector<unsigned char>&font,int x,int y,unsigned c,unsigned attr){
  if(font.size()!=4096||c>255)return;
  for(int yy=0;yy<16;++yy)for(int xx=0;xx<8;++xx)
   Put(x*8+xx,y*16+yy,palette[(font[c*16+yy]&(128>>xx))?(attr&15):((attr>>4)&7)]);
 }
 void Text(const std::vector<unsigned char>&font,int x,int y,const char *s,unsigned attr){
  for(;*s&&x<80;++s,++x)Char(font,x,y,(unsigned char)*s,attr);
 }
};
}
#endif
