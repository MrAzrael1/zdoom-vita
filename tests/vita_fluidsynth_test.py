"""Test actual MIDI adapter. Args: FluidSynth source dir, host library, SF2."""
from pathlib import Path
import sys, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
dep,lib,bank=map(Path,sys.argv[1:4])
source=(root/"src/sound/music_fluidsynth_mididevice.cpp").read_text()
start=source.index("void FluidSynthMIDIDevice::HandleEvent(")
end=source.index("//==========================================================================",start)
body=source[start:end].replace("FluidSynthMIDIDevice::HandleEvent(","HandleEvent(fluid_synth_t *FluidSynth, ",1)
fixture=r"""
#include <fluidsynth.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
enum { MIDI_NOTEOFF=0x80,MIDI_NOTEON=0x90,MIDI_POLYPRESS=0xa0,
 MIDI_CTRLCHANGE=0xb0,MIDI_PRGMCHANGE=0xc0,MIDI_CHANPRESS=0xd0,MIDI_PITCHBEND=0xe0 };
BODY
int main(int argc,char **argv){
 assert(argc==2);
 auto*settings=new_fluid_settings();assert(settings);
 fluid_settings_setnum(settings,"synth.sample-rate",44100);
 fluid_settings_setint(settings,"synth.reverb.active",0);
 fluid_settings_setint(settings,"synth.chorus.active",0);
 fluid_settings_setint(settings,"synth.polyphony",64);
 auto*synth=new_fluid_synth(settings);assert(synth);
 assert(fluid_synth_sfload(synth,argv[1],1)>=0);
 HandleEvent(synth,0xc0,0,0);
 unsigned int sf,bank,program;
 assert(fluid_synth_get_program(synth,0,&sf,&bank,&program)==FLUID_OK && program==0);
 HandleEvent(synth,0xb0,7,100);
 int cc;assert(fluid_synth_get_cc(synth,0,7,&cc)==FLUID_OK && cc==100);
 for(int bend : {0,8192,16383}) {
  HandleEvent(synth,0xe0,bend&127,bend>>7);
  int actual;assert(fluid_synth_get_pitch_bend(synth,0,&actual)==FLUID_OK && actual==bend);
 }
 HandleEvent(synth,0xe0,0,64);
 HandleEvent(synth,0x90,69,100);
 std::vector<float> output(44100*2);
 assert(fluid_synth_write_float(synth,44100,output.data(),0,2,output.data(),1,2)==FLUID_OK);
 double energy=0;
 for(float v:output){assert(std::isfinite(v));energy+=v*v;}
 assert(energy>.001);
 HandleEvent(synth,0x80,69,0);
 HandleEvent(synth,0x99,38,100);
 assert(fluid_synth_write_float(synth,512,output.data(),0,2,output.data(),1,2)==FLUID_OK);
 HandleEvent(synth,0x99,38,0);
 delete_fluid_synth(synth);delete_fluid_settings(settings);
 puts("PASS: bank load, program, volume, 14-bit pitch bend, note audio and percussion through actual adapter");
}
"""
fixture=fixture.replace("BODY",body)
with tempfile.TemporaryDirectory(prefix="vita-fluid-test-") as td:
 cpp=Path(td)/"test.cpp";exe=Path(td)/"test";cpp.write_text(fixture)
 subprocess.run(["g++","-std=c++17","-I"+str(dep/"include"),"-I"+str(lib.parent.parent/"include"),str(cpp),str(lib),
  "-lpthread","-lm","-o",str(exe)],check=True)
 subprocess.run([str(exe),str(bank)],check=True)