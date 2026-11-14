// Test the actual Vita mixer with a deterministic fake SDL device.
#include <cassert>
#include <cstdio>
#include <vector>
#include <fstream>
#include <iterator>
#include <string>
#include <mutex>
#include <thread>
#include <chrono>
#include <atomic>
#define I_VitaTrace(...) ((void)0)
#include "../src/c_cvars.h"
#undef EXTERN_CVAR
#define EXTERN_CVAR(type,name) extern bool name;
bool snd_pitched=false;
#include "../src/sound/sdl_sound.cpp"
static std::recursive_mutex queueLock; // fake device; the audio thread queues concurrently
static std::vector<short> queued;
static unsigned queuedBytes;
static std::atomic<int> ended;
static std::vector<FISoundChannel*> channels;
extern "C" {
int SDL_InitSubSystem(Uint32){return 0;}
void SDL_QuitSubSystem(Uint32){}
const char *SDL_GetError(){return "fake";}
const char *SDL_GetCurrentAudioDriver(){return "test";}
SDL_AudioDeviceID SDL_OpenAudioDevice(const char*,int,const SDL_AudioSpec*w,SDL_AudioSpec*h,int){*h=*w;return 1;}
void SDL_CloseAudioDevice(SDL_AudioDeviceID){}
void SDL_PauseAudioDevice(SDL_AudioDeviceID,int){}
void SDL_ClearQueuedAudio(SDL_AudioDeviceID){std::lock_guard<std::recursive_mutex> g(queueLock);queuedBytes=0;queued.clear();}
Uint32 SDL_GetQueuedAudioSize(SDL_AudioDeviceID){std::lock_guard<std::recursive_mutex> g(queueLock);return queuedBytes;}
int SDL_QueueAudio(SDL_AudioDeviceID,const void*d,Uint32 n){std::lock_guard<std::recursive_mutex> g(queueLock);const short*p=(const short*)d;queued.insert(queued.end(),p,p+n/2);queuedBytes+=n;return 0;}
Uint32 SDL_GetTicks(){return 100;}
SDL_RWops *SDL_RWFromConstMem(const void*,int){return NULL;}
SDL_AudioSpec *SDL_LoadWAV_RW(SDL_RWops*,int,SDL_AudioSpec*,Uint8**,Uint32*){return NULL;}
void SDL_FreeWAV(Uint8*){}
int SDL_BuildAudioCVT(SDL_AudioCVT*,SDL_AudioFormat,Uint8,int,SDL_AudioFormat,Uint8,int){return -1;}
int SDL_ConvertAudio(SDL_AudioCVT*){return -1;}
void *SDL_malloc(size_t n){return malloc(n);}
void SDL_free(void*p){free(p);}
void *SDL_memset(void*p,int c,size_t n){return memset(p,c,n);}
// Threading stubs. By default thread creation "fails", which exercises the
// game-thread fallback deterministically; CheckAudioThread() enables a real
// std::thread to exercise the audio-thread path (run under -fsanitize=thread).
static bool useRealThread=false;
struct SDL_mutex { std::mutex m; };
struct SDL_Thread { std::thread t; };
SDL_mutex *SDL_CreateMutex(){return new SDL_mutex;}
void SDL_DestroyMutex(SDL_mutex*m){delete m;}
int SDL_LockMutex(SDL_mutex*m){m->m.lock();return 0;}
int SDL_UnlockMutex(SDL_mutex*m){m->m.unlock();return 0;}
int SDL_AtomicSet(SDL_atomic_t*a,int v){return __atomic_exchange_n(&a->value,v,__ATOMIC_SEQ_CST);}
int SDL_AtomicGet(SDL_atomic_t*a){return __atomic_load_n(&a->value,__ATOMIC_SEQ_CST);}
SDL_Thread *SDL_CreateThreadWithStackSize(SDL_ThreadFunction fn,const char*,const size_t,void*data){
 if(!useRealThread)return NULL;
 auto*t=new SDL_Thread;t->t=std::thread([fn,data]{fn(data);});return t;
}
void SDL_WaitThread(SDL_Thread*t,int*){t->t.join();delete t;}
int SDL_SetThreadPriority(SDL_ThreadPriority){return 0;}
void SDL_Delay(Uint32 ms){std::this_thread::sleep_for(std::chrono::milliseconds(ms));}
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
// Minimal FileReader implementation for the in-memory stream ownership tests.
FileReader::FileReader():File(NULL),Length(0),StartPos(0),FilePos(0),CloseOnDestruct(false){}
FileReader::~FileReader(){}
long FileReader::Tell()const{return FilePos;}
long FileReader::Seek(long,int){return -1;}
long FileReader::Read(void*,long){return 0;}
char *FileReader::Gets(char*,int){return NULL;}
struct MusicReader : FileReader {
 std::vector<BYTE> bytes;
 MusicReader(const std::string&path){
  std::ifstream f(path,std::ios::binary);
  bytes.assign(std::istreambuf_iterator<char>(f),{});
  Length=bytes.size();
 }
 long Seek(long n,int origin) {
  long p=origin==SEEK_SET?n:origin==SEEK_CUR?FilePos+n:Length+n;
  if(p<0 || p>Length)return -1;FilePos=p;return 0;
 }
 long Read(void*out,long n){
  n=std::min(n,Length-FilePos);memcpy(out,bytes.data()+FilePos,n);FilePos+=n;return n;
 }
};
static void Drain(){std::lock_guard<std::recursive_mutex> g(queueLock);queuedBytes=0;queued.clear();}
static bool Synth(SoundStream*,void*b,int n,void*u){++*(int*)u;float*p=(float*)b;for(int i=0;i<n/4;++i)p[i]=.25f;return true;}
struct TinyMusic : VitaMusicDecoder {
 int position=0,total;
 TinyMusic(int hz,int ch,int n):total(n){rate=hz;channels=ch;}
 int Read(short *out,int requested) {
  int got=std::min(requested,total-position);
  for(int i=0;i<got*channels;++i)out[i]=1000;
  position+=got;return got;
 }
 bool Seek(unsigned ms){position=int(static_cast<unsigned long long>(ms)*rate/1000);return position<=total;}
};
static void CheckDigitalLifecycle() {
 for(int rate:{22050,44100,48000,96000})for(int ch:{1,2}) {
  DigitalStream music(NULL,NULL,new TinyMusic(rate,ch,17003));
  assert(music.Play(false,.5f));float l=0,r=0;int frames=0;
  while(music.ReadFrame(l,r)){assert(l>0 && l==r);++frames;}
  assert(std::abs(frames-int(std::ceil(17003.0*OutputRate/rate)))<=1);
  assert(music.IsEnded());
  assert(music.Play(true,1));music.SetPaused(true);assert(!music.ReadFrame(l,r));
  music.SetPaused(false);assert(music.ReadFrame(l,r));
  assert(music.SetPosition(100));assert(music.ReadFrame(l,r));
  music.Stop();assert(music.IsEnded()&&!music.ReadFrame(l,r));
 }
 DigitalStream tiny(NULL,NULL,new TinyMusic(44100,1,3));
 assert(tiny.Play(true,1));float l,r;
 for(int i=0;i<10000;++i)assert(tiny.ReadFrame(l,r)&&l>0&&l==r);
 DigitalStream empty(NULL,NULL,new TinyMusic(44100,1,0));
 assert(empty.Play(true,1));assert(!empty.ReadFrame(l,r)&&empty.IsEnded());
 puts("PASS: digital EOF, restart, mono/stereo rate conversion, seek, tiny loops and empty-loop guard");
}

static bool QueuedAudible(){std::lock_guard<std::recursive_mutex> g(queueLock);for(short v:queued)if(v)return true;return false;}
static bool Slow(SoundStream*,void*b,int n,void*u){
 ++*(std::atomic<int>*)u;std::this_thread::sleep_for(std::chrono::microseconds(200));
 float*p=(float*)b;for(int i=0;i<n/4;++i)p[i]=.25f;return true;
}
// Real audio thread: mixing happens without UpdateSounds(), finished voices are
// reaped on the game thread, and concurrent start/stop/unload/stream teardown
// is race-free (verified by ThreadSanitizer in the threaded build).
static void CheckAudioThread(){
 useRealThread=true;ended=0;Drain();
 auto*r=CreateSDLSoundRenderer();assert(r->IsValid());
 BYTE raw[]={128,255,0,128};
 auto s=r->LoadSoundRaw(raw,4,44100,1,8,-1);
 auto*loop=r->StartSound(s,1,128,SNDF_LOOP,NULL);assert(loop);
 bool audible=false;
 for(int i=0;i<200 && !audible;++i){std::this_thread::sleep_for(std::chrono::milliseconds(2));audible=QueuedAudible();}
 assert(audible); // mixed by the audio thread, no UpdateSounds() call
 r->StopChannel(loop);assert(!loop->SysChannel);
 auto*once=r->StartSound(s,1,128,0,NULL);assert(once);
 for(int i=0;i<500 && ended==1;++i){Drain();std::this_thread::sleep_for(std::chrono::milliseconds(1));r->UpdateSounds();}
 assert(ended>=2); // StopChannel + natural completion reaped on this thread
 SoundListener listener;listener.valid=true;listener.angle=0;listener.position=FVector3(0,0,0);
 FRolloffInfo rolloff={};
 std::vector<FISoundChannel*> live;
 auto stopOldest=[&]{r->StopChannel(live.front());live.erase(live.begin());};
 for(int i=0;i<4000;++i){
  auto*c=(i&1)?r->StartSound3D(s,&listener,1,&rolloff,1,128,0,FVector3(float(i%7),1,0),FVector3(0,0,0),0,SNDF_LOOP,NULL)
              :r->StartSound(s,.5f,128,SNDF_LOOP,NULL);
  if(c)live.push_back(c);
  // Touch every live voice while the audio thread is mixing them.
  for(auto*v:live){r->ChannelVolume(v,.25f+(i%4)*.1f);r->UpdateSoundParams3D(&listener,v,false,FVector3(1,float(i%5),0),FVector3(0,0,0));
   (void)r->GetPosition(v);(void)r->GetAudibility(v);}
  while(live.size()>12)stopOldest();
  if(i%16==0)std::this_thread::sleep_for(std::chrono::microseconds(500));
  if(i%64==0){r->SetSfxPaused(i%128==0,0);r->SetSfxVolume(.8f);r->SetMusicVolume(.7f);r->UpdateSounds();Drain();}
  if(i%500==0){while(!live.empty())stopOldest();r->UnloadSound(s);s=r->LoadSoundRaw(raw,4,44100,1,8,-1);}
 }
 while(!live.empty())stopOldest();
 r->SetSfxPaused(false,0);
 auto once2=r->StartSound(s,1,128,0,NULL);assert(once2);
 for(int i=0;i<200;++i){Drain();std::this_thread::sleep_for(std::chrono::milliseconds(1));r->UpdateSounds();if(!once2->SysChannel)break;}
 assert(!once2->SysChannel); // natural completion reaped while other work happened
 r->SetSfxPaused(false,0);
 std::atomic<int> calls(0);
 for(int i=0;i<200;++i){
  auto*stream=r->CreateStream(Slow,256,SoundStream::Float|SoundStream::Mono,22050,&calls);
  assert(stream && stream->Play(true,.5f));
  std::this_thread::sleep_for(std::chrono::microseconds(300));
  stream->SetVolume(.3f);(void)stream->GetPosition();(void)stream->IsEnded();
  if(i%3==0){Drain();}
  delete stream; // must not race with the audio thread inside Slow()
 }
 assert(calls>0);
 r->Sync(true);r->Sync(false);r->SetInactive(SoundRenderer::INACTIVE_Mute);r->SetInactive(SoundRenderer::INACTIVE_Active);
 r->UnloadSound(s);
 delete r; // joins the audio thread
 useRealThread=false;Drain();
 for(auto*c:channels)delete c;channels.clear();
 puts("PASS: audio thread mixes independently; game-thread reaping; concurrent start/stop/3D/unload; stream teardown during callbacks");fflush(stdout);
}
int main(int argc,char **argv){
 CheckDigitalLifecycle();
 if(argc==2 && std::string(argv[1])=="--threaded"){CheckAudioThread();return 0;}
 auto*r=CreateSDLSoundRenderer();assert(r->IsValid());
 if(argc==3 && std::string(argv[1])=="--music") {
  std::ifstream list(argv[2]);std::string path;unsigned tracks=0;
  while(list>>path) {
   MusicReader input(path);std::vector<BYTE> encoded=input.bytes;
   std::unique_ptr<VitaMusicDecoder> decoder(VitaOpenMusic(encoded));
   assert(decoder && decoder->rate==44100 && decoder->channels==2);
   short block[8192];unsigned long long frames=0,energy=0;int got;
   while((got=decoder->Read(block,4096))>0){
    frames+=got;for(int i=0;i<got*2;++i)energy+=std::abs(int(block[i]));
   }
   assert(frames>44100 && energy>0);
   assert(decoder->Seek(0) && decoder->Read(block,4096)>0);
   assert(decoder->Seek(1000) && decoder->Read(block,4096)>0);
   auto *music=r->OpenStream(new MusicReader(path),SoundStream::Loop);
   assert(music && music->Play(true,1));
   Drain();r->UpdateSounds();
   bool audible=false;
   for(int attempt=0;attempt<60 && !audible;++attempt) {
    for(short v:queued)if(v)audible=true;
    if(!audible){Drain();r->UpdateSounds();}
   }
   if(!audible)fprintf(stderr,"Silent music: %s\n",path.c_str());
   assert(audible);
   music->SetPaused(true);Drain();r->UpdateSounds();for(short v:queued)assert(!v);
   assert(music->SetPosition(1000));music->SetPaused(false);
   Drain();r->UpdateSounds();assert(!music->IsEnded());
   music->Stop();delete music;Drain();++tracks;
  }
  assert(tracks==80);
  BYTE invalid[]={0,1,2,3};std::vector<BYTE> junk(invalid,invalid+4);
  assert(!VitaOpenMusic(junk));assert(!r->OpenStream((FileReader*)NULL,0));
  printf("PASS: %u supplied Ogg/MP3 songs fully decoded; seek, mixing, pause and teardown\n",tracks);
 }
 if(argc==2) {
  std::ifstream manifest(argv[1]);std::string path;int rate,chans,play;
  unsigned count=0;
  while(manifest>>path>>rate>>chans>>play) {
   std::ifstream input(path,std::ios::binary);
   std::vector<BYTE> encoded((std::istreambuf_iterator<char>(input)),{});
   auto sample=r->LoadSound(encoded.data(),int(encoded.size()));
   if(!sample.data){fprintf(stderr,"decode failed: %s\n",path.c_str());assert(sample.data);}
   std::vector<short> pcm;int decodedRate=0,decodedChannels=0;
   assert(VitaDecodeVorbis(encoded.data(),int(encoded.size()),pcm,decodedRate,decodedChannels));
   assert(decodedRate==rate && decodedChannels==chans);
   size_t estimate=0;
   assert(VitaVorbisPCMBytes(encoded.data(),int(encoded.size()),estimate));
   assert(estimate>=pcm.size()*sizeof(short));
   assert(estimate>=pcm.size()/chans*4);
   assert(r->GetSampleLength(sample)==pcm.size()/chans);
   assert(r->GetMSLength(sample)>0);
   if(play) {
    assert(r->StartSound(sample,1,128,0,NULL));r->UpdateSounds();
    bool audible=false;for(short value:queued)if(value)audible=true;
    assert(audible);Drain();
   }
   r->UnloadSound(sample);++count;
  }
  assert(count>0);
  BYTE corrupt[]={'O','g','g','S',0,0,0,0};
  assert(!r->LoadSound(corrupt,sizeof(corrupt)).data);
  assert(!r->LoadSound(NULL,0).data);
  printf("PASS: %u actual Ogg effects decoded, native rates/channels retained, weapon PCM audible\n",count);
 }

 ended=0;Drain();
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
 // Pitch randomization is opt-in, including sounds routed through StartSound3D.
 s=r->LoadSoundRaw(raw,4,44100,1,8,-1);
 snd_pitched=false;
 auto*fixed=r->StartSound(s,1,64,SNDF_LOOP,NULL);
 r->UpdateSounds();assert(queued[2]>32000);Drain();
 r->StopChannel(fixed);
 snd_pitched=true;
 auto*pitched=r->StartSound(s,1,64,SNDF_LOOP,NULL);
 r->UpdateSounds();assert(queued[2]==0 && queued[4]>32000);Drain();
 r->StopChannel(pitched);
 auto*high=r->StartSound(s,1,192,SNDF_LOOP,NULL);
 r->UpdateSounds();assert(queued[2]>32000 && queued[4]==0);Drain();
 r->StopChannel(high);
 snd_pitched=false;r->UnloadSound(s);Drain();
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
