// Host checks of the shared input validation and physical touch mapping.
#include <cassert>
#include <limits>
#include <cstdio>
#include "../src/posix/vita_input_helpers.h"
#include "../src/posix/vita_scaling.h"
int main()
{
 unsigned short command[241]={ 'e','c','h','o',' ','"','h','i','"',0 };
 assert(I_VitaConsoleTextLength(command)==9);
 command[0]=0; assert(I_VitaConsoleTextLength(command)==0);
 for(int i=0;i<240;++i) command[i]='a';
 command[240]=0; assert(I_VitaConsoleTextLength(command)==240);
 command[240]='a'; assert(I_VitaConsoleTextLength(command)==-1);
 command[240]=0; command[20]='\n'; assert(I_VitaConsoleTextLength(command)==-1);
 command[20]=0xE9; assert(I_VitaConsoleTextLength(command)==-1);
 assert(I_VitaTouchPixel(-.2f,960)==0 && I_VitaTouchPixel(1.2f,960)==959);
 assert(I_VitaTouchPixel(std::numeric_limits<float>::quiet_NaN(),544)==0);
 for(int i=0;i<4;++i) {
  auto size=I_VitaRenderSize(i);
  // Inverse presentation mapping lands at the center in every preset.
  assert(I_VitaTouchPixel(.5f,960)*size.width/960==size.width/2);
  assert(I_VitaTouchPixel(.5f,544)*size.height/544==size.height/2);
  assert(I_VitaTouchPixel(1,960)*size.width/960<size.width);
  assert(I_VitaTouchPixel(1,544)*size.height/544<size.height);
 }
 puts("PASS: console character/length validation and touch mapping across all scaling presets");
}
