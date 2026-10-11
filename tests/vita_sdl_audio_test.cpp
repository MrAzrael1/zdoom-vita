// Test the actual Vita mixer with a deterministic fake SDL device.
#include <cassert>
#include <cstdio>
#include <vector>
#define I_VitaTrace(...) ((void)0)
#include "../src/sound/sdl_sound.cpp"
static std::vector<short> queued;
static unsigned queuedBytes;
static int ended;
static std::vector<FISoundChannel*> channels;
extern "C" {
int SDL_InitSubSystem(Uint32){return 0;}
void SDL_QuitSubSystem(Uint32){}
const char *SDL_GetError(){return "fake";}
const char *SDL_GetCurrentAudioDriver(){return "test";}
SDL_AudioDeviceID SDL_OpenAudioDevice(const char*,int,const SDL_AudioSpec*w,SDL_AudioSpec*h,int){*h=*w;return 1;}
void SDL_CloseAudioDevice(SDL_AudioDeviceID){}
void SDL_PauseAudioDevice(SDL_AudioDeviceID,int){}
void SDL_ClearQueuedAudio(SDL_AudioDeviceID){queuedBytes=0;queued.clear();}
Uint32 SDL_GetQueuedAudioSize(SDL_AudioDeviceID){return queuedBytes;}
int SDL_QueueAudio(SDL_AudioDeviceID,const void*d,Uint32 n){const short*p=(const short*)d;queued.insert(queued.end(),p,p+n/2);queuedBytes+=n;return 0;}
Uint32 SDL_GetTicks(){return 100;}
SDL_RWops *SDL_RWFromConstMem(const void*,int){return NULL;}
SDL_AudioSpec *SDL_LoadWAV_RW(SDL_RWops*,int,SDL_AudioSpec*,Uint8**,Uint32*){return NULL;}
void SDL_FreeWAV(Uint8*){}
int SDL_BuildAudioCVT(SDL_AudioCVT*,SDL_AudioFormat,Uint8,int,SDL_AudioFormat,Uint8,int){return -1;}
int SDL_ConvertAudio(SDL_AudioCVT*){return -1;}
void *SDL_malloc(size_t n){return malloc(n);}
void SDL_free(void*p){free(p);}
void *SDL_memset(void*p,int c,size_t n){return memset(p,c,n);}
}
FNullStringData FString::NullString={0,0,0,{0,0}};
FString::~FString(){--NullString.RefCount;}
SoundRenderer::SoundRenderer(){}
SoundRenderer::~SoundRenderer(){}
SoundStream::~SoundStream(){}
bool SoundStream::SetPosition(unsigned int){return false;}
bool SoundStream::SetOrder(int){return false;}
FString SoundStream::GetStats(){return FString();}
FString SoundRenderer::GatherStats(){return FString();}
short *SoundRenderer::DecodeSample(int,const void*,int,ECodecType){return NULL;}
void SoundRenderer::DrawWaveDebug(int){}
SoundDecoder *SoundRenderer::CreateDecoder(FileReader*){return NULL;}
SoundStream *SoundRenderer::OpenStream(const char*,int){return NULL;}
int Printf(const char*,...){return 0;}
FISoundChannel *S_GetChannel(void*s){auto*c=new FISoundChannel();c->SysChannel=s;channels.push_back(c);return c;}
void S_ChannelEnded(FISoundChannel*c){c->SysChannel=NULL;++ended;}
float S_GetRolloff(FRolloffInfo*,float,bool){return 1;}
static void Drain(){queuedBytes=0;queued.clear();}
static bool Synth(SoundStream*,void*b,int n,void*u){++*(int*)u;float*p=(float*)b;for(int i=0;i<n/4;++i)p[i]=.25f;return true;}
int main(){
 auto*r=CreateSDLSoundRenderer();assert(r->IsValid());
 BYTE raw[]={128,255,0,128};
 auto s=r->LoadSoundRaw(raw,4,44100,1,8,-1);
 assert(r->GetSampleLength(s)==4);
 r->StartSound(s,1,128,0,NULL);r->UpdateSounds();
 assert(queued.size()==6144 && queued[0]==0 && queued[1]==0);
 assert(queued[2]>32000 && queued[3]==queued[2]);
 assert(queued[4]==-32767 && queued[8]==0 && ended==1);Drain();
 auto*loop=r->StartSound(s,.5f,128,SNDF_LOOP,NULL);r->UpdateSounds();
 assert(queued[2]>16000 && queued[2]<16400 && loop->SysChannel);
 r->SetSfxPaused(true,0);Drain();r->UpdateSounds();for(short v:queued)assert(v==0);
 r->SetSfxPaused(false,0);r->StopChannel(loop);r->UnloadSound(s);Drain();
 BYTE stereo[]={0,64,0,192,0,0,0,0};
 s=r->LoadSoundRaw(stereo,8,22050,2,16,-1);r->StartSound(s,1,128,0,NULL);r->UpdateSounds();
 assert(queued[0]==16383 && queued[1]==-16383 && queued[2]==queued[0] && queued[3]==queued[1]);r->UnloadSound(s);Drain();
 int calls=0;auto*stream=r->CreateStream(Synth,64,SoundStream::Float|SoundStream::Mono,22050,&calls);
 assert(stream && stream->Play(true,.5f));r->SetMusicVolume(.5f);r->UpdateSounds();
 assert(calls>0 && queued[0]==2047 && queued[1]==2047);
 stream->SetPaused(true);Drain();r->UpdateSounds();for(short v:queued)assert(v==0);
 delete stream;Drain();r->UpdateSounds();for(short v:queued)assert(v==0);
 s=r->LoadSoundRaw(raw,4,44100,1,8,-1);
 for(int i=0;i<32;++i)assert(r->StartSound(s,1,128,SNDF_LOOP,NULL));
 assert(!r->StartSound(s,1,128,0,NULL));r->UnloadSound(s);delete r;
 for(auto*c:channels)delete c;
 puts("PASS: PCM, resampling, stereo, completion, loops, pause, synth gain, stream lifetime and voice limit");
}
