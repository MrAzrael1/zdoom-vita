// Conservative distance checks for optional Vita visual culling.
// Work in 1/256 map units to keep squared deltas safe across the full 16.16
// coordinate range. Equality and sub-unit rounding favor retaining graphics.
#ifndef VITA_DISTANCE_CULL_H
#define VITA_DISTANCE_CULL_H
#include <stdint.h>
#include <algorithm>
namespace VitaCull {
inline bool OutsidePoint(int32_t x,int32_t y,int32_t z,int32_t vx,int32_t vy,int32_t vz,
 int distance,int32_t radius=0) {
 int64_t dx=(int64_t(x)-vx)/256,dy=(int64_t(y)-vy)/256,dz=(int64_t(z)-vz)/256;
 int64_t limit=int64_t(distance)*256+(int64_t(std::max(int32_t(0),radius))+255)/256;
 return dx*dx+dy*dy+dz*dz>limit*limit;
}
// A segment is rejected only when its complete XY bounding box is outside the
// range. This deliberately retains some diagonal walls; endpoint-only checks
// would incorrectly remove long walls that pass close to the camera.
inline bool OutsideSegment(int32_t x1,int32_t y1,int32_t x2,int32_t y2,
 int32_t vx,int32_t vy,int distance) {
 int32_t minx=std::min(x1,x2),maxx=std::max(x1,x2);
 int32_t miny=std::min(y1,y2),maxy=std::max(y1,y2);
 int64_t dx=vx<minx?int64_t(minx)-vx:vx>maxx?int64_t(vx)-maxx:0;
 int64_t dy=vy<miny?int64_t(miny)-vy:vy>maxy?int64_t(vy)-maxy:0;
 dx/=256;dy/=256;
 int64_t limit=int64_t(distance)*256;
 return dx*dx+dy*dy>limit*limit;
}
}
#endif
