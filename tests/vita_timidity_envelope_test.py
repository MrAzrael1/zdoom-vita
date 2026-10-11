"""Exercise the actual TiMidity mixer and ComputeOutput with a constant sample.
No SoundFont file or SDL device is needed: compare render block sizes."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/timidity/timidity.cpp").read_text()
start = source.index("void Renderer::ComputeOutput(")
end = source.index("\nvoid Renderer::MarkInstrument", start)
body = source[start:end]
fixture = r"""
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "timidity/mix.cpp"
void *M_Realloc(void *p, size_t n) { return realloc(p,n); }
namespace Timidity {
Renderer::Renderer(float hz, const char *) {
 rate=hz; control_ratio=int(hz/CONTROLS_PER_SECOND);
 voices=1; voice=new Voice[1](); resample_buffer=NULL; resample_buffer_size=0;
}
Renderer::~Renderer(){ delete[] voice; free(resample_buffer); }
sample_t *resample_voice(Renderer *song, Voice *, int *n) {
 for(int i=0;i<*n;++i) song->resample_buffer[i]=1;
 return song->resample_buffer;
}
BODY
}
using namespace Timidity;
static std::vector<float> Render(int block, bool shaped=false) {
 Renderer song(44100,NULL);
 Sample sample={};
 sample.type=INST_SF2;
 sample.envelope.sf2.delay_vol=-12000; // 0.977 ms
 sample.envelope.sf2.attack_vol=-32768; // immediate
 sample.envelope.sf2.hold_vol=-1200; // 0.5 s
 sample.envelope.sf2.decay_vol=-32768;
 sample.envelope.sf2.sustain_vol=shaped ? 600 : 0;
 if(shaped) { sample.envelope.sf2.attack_vol=-6800; sample.envelope.sf2.hold_vol=-6800; }
 Voice &v=song.voice[0]; v.sample=&sample; v.status=VOICE_RUNNING;
 v.left_offset=v.right_offset=1;
 v.eg1.Init(&song,&v);
 std::vector<float> result(4410*2);
 for(int i=0;i<4410;i+=block) song.ComputeOutput(result.data()+i*2, std::min(block,4410-i));
 return result;
}
int main(){
 auto large=Render(4410), small=Render(44);
 int firstLarge=-1,firstSmall=-1;
 for(int i=0;i<4410;++i) {
  if(firstLarge<0 && large[i*2]>.01f)firstLarge=i;
  if(firstSmall<0 && small[i*2]>.01f)firstSmall=i;
 }
 printf("First audible sample: 100ms block=%d, 1ms block=%d\n",firstLarge,firstSmall);
 assert(firstLarge>=0 && firstLarge<132);
 assert(firstSmall>=0 && abs(firstLarge-firstSmall)<=44);
 for(int i=132;i<4410;++i) assert(fabs(large[i*2]-small[i*2])<.00001);
  auto attackLarge=Render(4410,true),attackSmall=Render(44,true);
 for(size_t i=0;i<attackLarge.size();++i) assert(fabs(attackLarge[i]-attackSmall[i])<.00001);
 assert(attackLarge[441*2]>attackLarge[44*2]);
 assert(attackLarge[4000*2]<attackLarge[1400*2]);
 puts("PASS: SF2 delay, attack, hold, sustain and stereo levels are independent of callback size");
}
"""
fixture = fixture.replace("BODY", body)
with tempfile.TemporaryDirectory(prefix="vita-timidity-") as td:
    cpp = Path(td)/"test.cpp"
    exe = Path(td)/"test"
    cpp.write_text(fixture)
    subprocess.run(["g++","-std=c++17","-DNOASM","-Dstricmp=strcasecmp","-Dstrnicmp=strncasecmp",
        "-I"+str(root/"src"),"-I"+str(root/"src/timidity"),"-I"+str(root/"zlib"),
        "-I"+str(root/"bzip2"),"-fsanitize=address,undefined","-fno-omit-frame-pointer",
        str(cpp),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True)