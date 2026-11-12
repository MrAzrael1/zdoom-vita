// Paletted world-view enlargement; UI is drawn afterward at framebuffer size.
#ifndef VITA_WORLD_DETAIL_H
#define VITA_WORLD_DETAIL_H
#include <vector>
#include <cstring>
#include <stdint.h>
class FVitaWorldScaler {
 std::vector<int> columns;
 int oldSource=0;
public:
 void Blit(unsigned char *dst,int pitch,int width,int height,
  const unsigned char *src,int sourcePitch,int sourceWidth,int sourceHeight) {
  if(!dst || !src || width<=0 || height<=0 || sourceWidth<=0 || sourceHeight<=0 ||
   pitch<width || sourcePitch<sourceWidth)return;
  if(int(columns.size())!=width || oldSource!=sourceWidth){
   columns.resize(width);oldSource=sourceWidth;
   for(int x=0;x<width;++x)columns[x]=int(int64_t(x)*sourceWidth/width);
  }
  int last=-1;
  for(int y=0;y<height;++y){
   int row=int(int64_t(y)*sourceHeight/height);
   if(row==last)memcpy(dst+y*pitch,dst+(y-1)*pitch,width);
   else {
    const unsigned char *input=src+row*sourcePitch;unsigned char *output=dst+y*pitch;
    for(int x=0;x<width;++x)output[x]=input[columns[x]];
   }
   last=row;
  }
 }
};
#endif
