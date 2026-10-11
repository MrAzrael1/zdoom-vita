"""Check actual SF2 region conversion against sample tuning metadata."""
from pathlib import Path
import subprocess, tempfile, struct
root=Path(__file__).resolve().parents[1]
source=(root/"src/timidity/instrum_sf2.cpp").read_text(encoding='latin1')
start=source.index("void SFFile::ApplyGeneratorsToRegion(")
end=source.index("//===========================================================================",start)
body=source[start:end].replace("SFFile::ApplyGeneratorsToRegion","ApplyGeneratorsToRegion",1)
fixture=r"""
#include <cassert>
#include <cstdio>
#include <cmath>
#include <initializer_list>
#include "timidity/timidity.h"
#include "timidity/sf2.h"
#include "templates.h"
namespace Timidity {
BODY
}
int main() {
 using namespace Timidity;
 float data[64]={};
 SFGenComposite gen={};
 gen.overridingRootKey=-1; gen.keynum=-1; gen.scaleTuning=100;
 gen.coarseTune=1; gen.fineTune=-12;
 SFSample sample={};
 sample.Start=0;sample.End=32;sample.StartLoop=4;sample.EndLoop=28;
 sample.SampleRate=22050; sample.OriginalPitch=60;sample.InMemoryData=data;
 for(int root : {60,69}) for(int correction : {-50,0,50}) {
  gen.overridingRootKey=root==60 ? -1 : root;
  sample.PitchCorrection=correction;
  Sample region={};
  ApplyGeneratorsToRegion(&gen,&sample,NULL,&region);
  double expected=8175.798947309669*std::exp2((root-correction*.01)/12.0);
  double error=1200*std::log2(region.root_freq/expected);
  printf("root=%d correction=%d: pitch error %.3f cents\n",root,correction,error);
  fflush(stdout);
  assert(std::abs(error)<.001);
  assert(region.tune==88); // Generator tuning remains a separate adjustment.
  assert(region.sample_rate==22050);
 }
 puts("PASS: signed SF2 sample correction, root override, and generator tuning");
}
"""
fixture=fixture.replace("BODY",body)
with tempfile.TemporaryDirectory(prefix="vita-pitch-") as td:
 cpp=Path(td)/"test.cpp";exe=Path(td)/"test";cpp.write_text(fixture)
 subprocess.run(["g++","-std=c++17","-DNOASM","-Dstricmp=strcasecmp","-Dstrnicmp=strncasecmp",
  "-I"+str(root/"src"),"-I"+str(root/"zlib"),"-I"+str(root/"bzip2"),
  "-fsanitize=address,undefined","-fno-omit-frame-pointer",str(cpp),"-o",str(exe)],check=True)
 subprocess.run([str(exe)],check=True)