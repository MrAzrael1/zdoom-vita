// Vita SDL2 audio backend.
//
// Threading model (v0.13 audio thread):
//  - A dedicated audio thread (pinned to Vita user core 1) mixes effect voices,
//    pulls PCM from music streams (FluidSynth, Ogg/MP3, GME, ...) and keeps
//    SDL's queue topped up. SDL itself still consumes the queue on its own
//    internal audio thread.
//  - The game thread only issues commands (start/stop/volume/position) and
//    reaps finished voices, because S_ChannelEnded and the rest of the sound
//    channel bookkeeping in s_sound.cpp are not thread-safe.
//  - voiceLock guards voices[], volumes, pause/inactive/sync state.
//    streamLock guards streams[] and every PCMStream's state, and is held while
//    stream callbacks generate audio. The two locks are never held together,
//    so effects never wait on a slow synth callback.
//  - Music devices already guard their own state with FCriticalSection (stock
//    ZDoom ran these callbacks on FMOD's mixer thread). Lock order is always
//    streamLock -> device CritSec; no device calls back into this renderer
//    while holding its CritSec.
//  - If the thread cannot be created, mixing falls back to the game thread in
//    UpdateSounds(), exactly as before.
#include <SDL.h>
#include <SDL_thread.h>
#include <vector>
#include <memory>
#include <cmath>
#include <cstring>
#include "i_sound.h"
#include "c_cvars.h"
#include "vita_vorbis.h"
#include "files.h"
#include "posix/vita_debug.h"
#include "posix/vita_perf.h"
#ifdef ZD_VITA
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/cpu.h>
#endif

EXTERN_CVAR (Bool, snd_pitched)

namespace {
const int OutputRate = 44100;
const int MixFrames = 512;
const int VoiceCount = 32;
const Uint32 QueueTargetBytes = 3072*4; // ~70ms of 16-bit stereo, as before.

// Null-safe scoped SDL mutex lock.
class FAudioGuard {
	SDL_mutex *mutex;
public:
	explicit FAudioGuard(SDL_mutex *m) : mutex(m) { if (mutex) SDL_LockMutex(mutex); }
	~FAudioGuard() { if (mutex) SDL_UnlockMutex(mutex); }
};

struct Sample {
	std::vector<short> pcm;
	int rate, channels, loopStart, loopEnd;
	int Frames() const { return int(pcm.size()) / channels; }
};

class SDLSoundRenderer;
class PCMStream : public SoundStream {
public:
	SDLSoundRenderer *owner;
	SDL_mutex *lock; // owner's streamLock; NULL once the owner is gone.
	~PCMStream();
	SoundStreamCallback callback;
	void *userdata;
	std::vector<BYTE> buffer;
	int flags, rate, channels, bytesPerSample, frames;
	double cursor, step;
	unsigned int playedFrames;
	float volume;
	bool active, paused, ended;
	PCMStream(SDLSoundRenderer *renderer, SDL_mutex *streamLock, SoundStreamCallback cb, int bytes, int fmt, int hz, void *ud)
		: owner(renderer), lock(streamLock), callback(cb), userdata(ud), flags(fmt), rate(hz),
		  channels((fmt & Mono) ? 1 : 2),
		  bytesPerSample((fmt & Float) || (fmt & Bits32) ? 4 : (fmt & Bits8) ? 1 : 2),
		  cursor(0), step(double(hz)/OutputRate), playedFrames(0), volume(1), active(false), paused(false), ended(false)
	{
		frames = bytes / (channels * bytesPerSample);
		buffer.resize(frames * channels * bytesPerSample);
		cursor = frames;
	}
	// Removes this stream from the mixer. After it returns the audio thread
	// can no longer call into this stream. Safe to call more than once.
	void Detach();
	// *Locked helpers assume streamLock is already held.
	bool PlayLocked(float vol) { volume=vol; active=true; paused=false; ended=false; cursor=frames; playedFrames=0; return true; }
	void StopLocked() { active=false; ended=true; }
	virtual bool Play(bool, float vol) { FAudioGuard g(lock); return PlayLocked(vol); }
	void Stop() { FAudioGuard g(lock); StopLocked(); }
	void SetVolume(float vol) { FAudioGuard g(lock); volume=vol; }
	bool SetPaused(bool value) { FAudioGuard g(lock); paused=value; return true; }
	unsigned int GetPosition() { FAudioGuard g(lock); return playedFrames * 1000ULL / OutputRate; }
	bool IsEnded() { FAudioGuard g(lock); return ended; }
	float ReadValue(int index) {
		const BYTE *p = &buffer[index * bytesPerSample];
		if (flags & Float) { float value; memcpy(&value,p,4); return std::isfinite(value) ? value : 0; }
		if (bytesPerSample == 1) return (int(*p)-128)/128.f;
		if (bytesPerSample == 2) { short value; memcpy(&value,p,2); return value/32768.f; }
		int value; memcpy(&value,p,4); return value/2147483648.f;
	}
	// Called by the mixer with streamLock held. The callback may run on the
	// audio thread, so performance scopes (not thread-safe) are not used here.
	bool ReadFrame(float &left, float &right) {
		if (!active || paused) return false;
		while (cursor >= frames) {
			double carry = cursor - frames;
			memset(buffer.data(),(flags & Bits8) ? 128 : 0,buffer.size());
			bool filled = callback(this, buffer.data(), int(buffer.size()), userdata);
			if (!filled) { StopLocked(); return false; }
			cursor = carry;
		}
		int index = int(cursor);
		if (index >= frames) { StopLocked(); return false; }
		left = ReadValue(index*channels)*volume;
		right = channels == 2 ? ReadValue(index*channels+1)*volume : left;
		cursor += step;
		++playedFrames;
		return true;
	}
};

// Incremental decoding runs wherever the mixer runs (normally the audio thread).
class DigitalStream : public PCMStream {
 VitaMusicDecoder *decoder;
 bool looping;
 static bool Fill(SoundStream *base,void *out,int bytes,void*) {
  DigitalStream *self=static_cast<DigitalStream*>(base);
  short *pcm=static_cast<short*>(out);
  int capacity=bytes/(2*self->channels),got=0;
  bool rewoundWithoutProgress=false;
  while(got<capacity) {
   int n=self->decoder->Read(pcm+got*self->channels,capacity-got);
   if(n>0){got+=n;rewoundWithoutProgress=false;continue;}
   if(!self->looping || rewoundWithoutProgress || !self->decoder->Seek(0))break;
   rewoundWithoutProgress=true;
  }
  self->frames=got; // No padded silence at EOF, including short tracks.
  return got>0;
 }
public:
 DigitalStream(SDLSoundRenderer *renderer,SDL_mutex *streamLock,VitaMusicDecoder *source):
  PCMStream(renderer,streamLock,Fill,4096*source->channels,source->channels==1?Mono:0,source->rate,NULL),
  decoder(source),looping(false) {}
 // Detach first so the audio thread cannot be inside Fill() while the
 // decoder is deleted.
 ~DigitalStream(){Detach();delete decoder;}
 bool Play(bool loop,float vol) {
  FAudioGuard g(lock);
  if(!decoder->Seek(0))return false;
  looping=loop;frames=int(buffer.size())/(channels*bytesPerSample);
  return PlayLocked(vol);
 }
 bool SetPosition(unsigned milliseconds) {
  FAudioGuard g(lock);
  if(!decoder->Seek(milliseconds))return false;
  frames=int(buffer.size())/(channels*bytesPerSample);cursor=frames;
  playedFrames=static_cast<unsigned long long>(milliseconds)*OutputRate/1000;
  ended=false;return true;
 }
};

class SDLSoundRenderer : public SoundRenderer {
	struct Voice {
		Sample *sample;
		FISoundChannel *channel;
		double position, step;
		float volume, left, right;
		int flags;
		bool finished;
		Voice() : sample(NULL), channel(NULL), position(0), step(1), volume(1), left(1), right(1), flags(0), finished(false) {}
	};
	SDL_AudioDeviceID device;
	Voice voices[VoiceCount];
	std::vector<PCMStream*> streams;
	float sfxVolume, musicVolume;
	unsigned int pauseSlots;
	EInactiveState inactive;
	bool syncing;
	SDL_mutex *voiceLock;
	SDL_mutex *streamLock;
	SDL_Thread *mixThread;
	SDL_atomic_t mixRunning;

	Voice *GetVoice(FISoundChannel *chan) {
		return chan ? static_cast<Voice*>(chan->SysChannel) : NULL;
	}
	// Game thread only. S_ChannelEnded may call GetPosition(), so it runs
	// without voiceLock held; only the game thread ever clears a voice, so
	// the voice cannot be reused in between.
	void EndVoice(Voice &voice) {
		FISoundChannel *chan;
		{ FAudioGuard g(voiceLock); chan = voice.channel; }
		if (chan) S_ChannelEnded(chan); // GetPosition must still see the final position.
		FAudioGuard g(voiceLock);
		if (voice.channel == chan) voice = Voice();
	}
	// Caller holds voiceLock.
	void Spatial(Voice &voice, SoundListener *listener, const FVector3 &pos) {
		if (!listener || !listener->valid) { voice.left=voice.right=0; return; }
		FVector3 delta = pos - listener->position;
		float distance = std::sqrt(delta.X*delta.X + delta.Y*delta.Y + delta.Z*delta.Z);
		float gain = S_GetRolloff(&voice.channel->Rolloff, distance*voice.channel->DistanceScale, false);
		// Listener angle is in radians, with Y increasing to the listener's left.
		float pan = distance > 0 ? (std::sin(listener->angle)*delta.X - std::cos(listener->angle)*delta.Y)/distance : 0;
		if (pan < -1) pan=-1;
		if (pan > 1) pan=1;
		voice.left = gain*(pan > 0 ? 1-pan : 1);
		voice.right = gain*(pan < 0 ? 1+pan : 1);
	}

	// Mixes and queues one block. Returns false if nothing was queued
	// (synchronising, fully inactive, or a queue error).
	bool MixBlock() {
		float mix[MixFrames*2];
		memset(mix, 0, sizeof(mix));
		float streamVolume;
		bool mute;
		{
			FAudioGuard g(voiceLock);
			if (syncing || inactive==INACTIVE_Complete) return false;
			for(int v=0;v<VoiceCount;++v) {
				Voice &voice=voices[v];
				if(!voice.channel || voice.finished || (pauseSlots && !(voice.flags&SNDF_NOPAUSE)))continue;
				Sample &s=*voice.sample;
				const int end=(voice.flags&SNDF_LOOP)?s.loopEnd:s.Frames();
				const int channels=s.channels;
				const short *pcm=s.pcm.data();
				const double step=voice.step;
				const float gainL=voice.volume*voice.left*sfxVolume;
				const float gainR=voice.volume*voice.right*sfxVolume;
				double position=voice.position;
				for(int i=0;i<MixFrames;++i) {
					if(position>=end) {
						if(voice.flags&SNDF_LOOP) position=s.loopStart+std::fmod(position-end,double(end-s.loopStart));
						else { voice.finished=true; break; }
					}
					int frame=int(position), index=frame*channels;
					float l=pcm[index]/32768.f;
					float r=channels==2 ? pcm[index+1]/32768.f : l;
					mix[i*2]+=l*gainL;
					mix[i*2+1]+=r*gainR;
					position+=step;
				}
				voice.position=position;
			}
			streamVolume=musicVolume;
			mute=inactive==INACTIVE_Mute;
		}
		{
			FAudioGuard g(streamLock);
			for(size_t s=0;s<streams.size();++s) for(int i=0;i<MixFrames;++i) {
				float l=0,r=0;
				if(streams[s]->ReadFrame(l,r)) { mix[i*2]+=l*streamVolume; mix[i*2+1]+=r*streamVolume; }
			}
		}
		short output[MixFrames*2];
		for(int i=0;i<MixFrames*2;++i) {
			float value=mute ? 0 : mix[i];
			if(value>1)value=1;
			if(value<-1)value=-1;
			output[i]=short(value*32767);
		}
		if(SDL_QueueAudio(device,output,sizeof(output))!=0) { I_VitaTrace("[Vita] Audio queue failed: %s\n",SDL_GetError()); return false; }
		return true;
	}
	// Tops up SDL's queue to the target, at most six blocks per call.
	void PumpAudio() {
		for(int block=0;block<6 && SDL_GetQueuedAudioSize(device)<QueueTargetBytes;++block)
			if(!MixBlock()) break;
	}
	static int MixThreadMain(void *data) {
		SDLSoundRenderer *self=static_cast<SDLSoundRenderer*>(data);
#ifdef ZD_VITA
		int affinity=sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_1);
		I_VitaTrace("[Vita] Audio thread started (core 1 affinity result 0x%08X)\n", (unsigned)affinity);
#endif
		SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);
		while(SDL_AtomicGet(&self->mixRunning)) {
			self->PumpAudio();
			SDL_Delay(2); // A block is ~11.6ms; polling every 2ms keeps the queue full.
		}
		return 0;
	}
	void StartMixThread() {
		voiceLock=SDL_CreateMutex();
		streamLock=SDL_CreateMutex();
		if(!voiceLock || !streamLock) {
			I_VitaTrace("[Vita] Audio thread disabled: mutex creation failed: %s\n", SDL_GetError());
			if(voiceLock) SDL_DestroyMutex(voiceLock);
			if(streamLock) SDL_DestroyMutex(streamLock);
			voiceLock=streamLock=NULL;
			return;
		}
		SDL_AtomicSet(&mixRunning,1);
		mixThread=SDL_CreateThreadWithStackSize(MixThreadMain,"zdoom-audio",256*1024,this);
		if(!mixThread) {
			SDL_AtomicSet(&mixRunning,0);
			I_VitaTrace("[Vita] Audio thread creation failed, mixing on game thread: %s\n", SDL_GetError());
		}
	}
public:
	SDLSoundRenderer() : device(0), sfxVolume(1), musicVolume(1), pauseSlots(0), inactive(INACTIVE_Active), syncing(false),
		voiceLock(NULL), streamLock(NULL), mixThread(NULL) {
		SDL_AtomicSet(&mixRunning,0);
		if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) { I_VitaTrace("[Vita] Audio init failed: %s\n", SDL_GetError()); return; }
		SDL_AudioSpec want, have;
		SDL_zero(want);
		want.freq=OutputRate; want.format=AUDIO_S16SYS; want.channels=2; want.samples=MixFrames;
		device = SDL_OpenAudioDevice(NULL,0,&want,&have,0);
		if (!device) { I_VitaTrace("[Vita] Audio open failed: %s\n", SDL_GetError()); return; }
		I_VitaTrace("[Vita] Audio: %s, %d Hz stereo, %d samples\n", SDL_GetCurrentAudioDriver(), have.freq, have.samples);
#ifdef ZD_VITA
		// Keep the game thread on core 0 so it never shares a core with the mixer.
		int mainAffinity=sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), SCE_KERNEL_CPU_MASK_USER_0);
		I_VitaTrace("[Vita] Game thread core 0 affinity result 0x%08X\n", (unsigned)mainAffinity);
#endif
		StartMixThread();
		SDL_PauseAudioDevice(device,0);
	}
	~SDLSoundRenderer() {
		if(mixThread) { SDL_AtomicSet(&mixRunning,0); SDL_WaitThread(mixThread,NULL); mixThread=NULL; }
		if(device) SDL_CloseAudioDevice(device);
		for(size_t i=0;i<streams.size();++i) { streams[i]->owner=NULL; streams[i]->lock=NULL; }
		streams.clear();
		if(voiceLock) SDL_DestroyMutex(voiceLock);
		if(streamLock) SDL_DestroyMutex(streamLock);
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
	}
	bool IsValid() { return device != 0; }
	void SetSfxVolume(float vol) { FAudioGuard g(voiceLock); sfxVolume=vol; }
	void SetMusicVolume(float vol) { FAudioGuard g(voiceLock); musicVolume=vol; }
	SoundHandle LoadSoundRaw(BYTE *data,int length,int rate,int channels,int bits,int loopStart,int loopEnd) {
		SoundHandle result={NULL};
		if(!data || length<=0 || rate<=0 || (channels!=1 && channels!=2) || (bits!=8 && bits!=16)) return result;
		Sample *sample = new Sample;
		sample->rate=rate; sample->channels=channels;
		int count=length/(bits/8);
		sample->pcm.resize(count);
		for(int i=0;i<count;++i) {
			if(bits==8) sample->pcm[i]=(int(data[i])-128)*256;
			else sample->pcm[i]=short(unsigned(data[i*2]) | (unsigned(data[i*2+1])<<8));
		}
		sample->loopStart=loopStart < 0 ? 0 : loopStart;
		sample->loopEnd=loopEnd <= 0 || loopEnd>sample->Frames() ? sample->Frames() : loopEnd;
		if(sample->loopStart>=sample->loopEnd) sample->loopStart=0;
		if(sample->Frames()==0) { delete sample; return result; }
		result.data=sample;
		return result;
	}
	SoundHandle LoadSound(BYTE *data,int length) {
#ifdef ZD_VITA
		FVitaPerfScope vitaSoundDecode(VPERF_SOUND_LOAD);
#endif
		SoundHandle result={NULL};
		if(!data || length<=0)return result;
		if(length>=4 && memcmp(data,"OggS",4)==0) {
			Sample *sample=new Sample;
			if(!VitaDecodeVorbis(data,length,sample->pcm,sample->rate,sample->channels)) {
				delete sample;I_VitaTrace("[Vita] Audio: Ogg effect decode failed (%d bytes)\n",length);
				return result;
			}
			sample->loopStart=0;sample->loopEnd=sample->Frames();
			result.data=sample;return result;
		}
		SDL_AudioSpec spec;
		Uint8 *pcm=NULL; Uint32 bytes=0;
		if(!SDL_LoadWAV_RW(SDL_RWFromConstMem(data,length),1,&spec,&pcm,&bytes)) return result;
		if(spec.channels==1 || spec.channels==2) {
			SDL_AudioCVT cvt;
			if(SDL_BuildAudioCVT(&cvt,spec.format,spec.channels,spec.freq,AUDIO_S16SYS,spec.channels,spec.freq)>=0) {
				cvt.len=bytes; cvt.buf=(Uint8*)SDL_malloc(bytes*cvt.len_mult);
				if(cvt.buf) {
					memcpy(cvt.buf,pcm,bytes);
					if(!cvt.needed) cvt.len_cvt=cvt.len;
					if(!cvt.needed || SDL_ConvertAudio(&cvt)==0) result=LoadSoundRaw(cvt.buf,cvt.len_cvt,spec.freq,spec.channels,16,-1,-1);
					SDL_free(cvt.buf);
				}
			}
		}
		SDL_FreeWAV(pcm);
		return result;
	}
	void UnloadSound(SoundHandle sound) {
		Sample *sample=static_cast<Sample*>(sound.data);
		for(int i=0;i<VoiceCount;++i) {
			bool uses;
			{ FAudioGuard g(voiceLock); uses=voices[i].sample==sample && voices[i].channel; }
			if(uses) EndVoice(voices[i]);
		}
		// No voice references the sample any more, and mixing only happens
		// under voiceLock, so the audio thread cannot be reading it.
		delete sample;
	}
	unsigned int GetMSLength(SoundHandle sound) { Sample *s=(Sample*)sound.data; return s ? s->Frames()*1000ULL/s->rate : 0; }
	unsigned int GetSampleLength(SoundHandle sound) { Sample *s=(Sample*)sound.data; return s ? s->Frames() : 0; }
	float GetOutputRate() { return OutputRate; }
	SoundStream *CreateStream(SoundStreamCallback cb,int bytes,int flags,int rate,void *ud) {
		int unit=((flags&SoundStream::Mono)?1:2)*((flags&(SoundStream::Float|SoundStream::Bits32))?4:(flags&SoundStream::Bits8)?1:2);
		if(!cb || bytes<unit || rate<=0 || rate>192000) return NULL;
		PCMStream *stream=new PCMStream(this,streamLock,cb,bytes,flags,rate,ud);
		FAudioGuard g(streamLock);
		streams.push_back(stream);
		return stream;
	}
	SoundStream *OpenStream(FileReader *reader,int) {
		// Keep compressed music, not a fully expanded song. FileReader can
		// represent a WAD range or a decompressed PK3 entry.
		std::unique_ptr<FileReader> input(reader);
		if(!reader)return NULL;
		long length=reader->GetLength();
		if(length<4 || length>64*1024*1024) {
			I_VitaTrace("[Vita] Music: rejected compressed stream size %ld\n",length);
			return NULL;
		}
		try {
			std::vector<unsigned char> encoded(length);
			if(reader->Seek(0,SEEK_SET)!=0 || reader->Read(encoded.data(),length)!=length)return NULL;
			std::unique_ptr<VitaMusicDecoder> decoder(VitaOpenMusic(encoded));
			if(!decoder) {
				I_VitaTrace("[Vita] Music: unsupported or invalid digital stream\n");
				return NULL;
			}
			std::unique_ptr<DigitalStream> stream(new DigitalStream(this,streamLock,decoder.get()));
			decoder.release();
			FAudioGuard g(streamLock);
			streams.push_back(stream.get());
			return stream.release();
		} catch(const std::bad_alloc&) {
			I_VitaTrace("[Vita] Music: insufficient memory for compressed stream\n");
			return NULL;
		}
	}
	// Caller holds voiceLock.
	FISoundChannel *StartSoundLocked(SoundHandle sound,float vol,int pitch,int flags,FISoundChannel *reuse) {
		if(!sound.data) return NULL;
		Voice *voice=NULL;
		for(int i=0;i<VoiceCount;++i) if(!voices[i].channel) { voice=&voices[i]; break; }
		if(!voice) return NULL;
		voice->sample=(Sample*)sound.data;
		voice->position=0; voice->finished=false; voice->left=voice->right=1;
		voice->volume=vol; voice->flags=flags;
		voice->step=double(voice->sample->rate)/OutputRate * (snd_pitched && pitch>0 ? pitch/128.0 : 1.0);
		voice->channel=reuse ? reuse : S_GetChannel(voice);
		voice->channel->SysChannel=voice;
		voice->channel->StartTime.AsOne=SDL_GetTicks();
		voice->channel->DistanceScale=1;
		voice->channel->DistanceSqr=0;
		voice->channel->ManualRolloff=false;
		return voice->channel;
	}
	FISoundChannel *StartSound(SoundHandle sound,float vol,int pitch,int flags,FISoundChannel *reuse) {
		FAudioGuard g(voiceLock);
		return StartSoundLocked(sound,vol,pitch,flags,reuse);
	}
	FISoundChannel *StartSound3D(SoundHandle sound,SoundListener *listener,float vol,FRolloffInfo *rolloff,float scale,int pitch,int,const FVector3 &pos,const FVector3 &,int,int flags,FISoundChannel *reuse) {
		// One lock for start + spatialisation, so the mixer never hears a
		// positional sound at full centred volume for a block.
		FAudioGuard g(voiceLock);
		FISoundChannel *chan=StartSoundLocked(sound,vol,pitch,flags,reuse);
		if(chan) { chan->Rolloff=*rolloff; chan->DistanceScale=scale; Spatial(*GetVoice(chan),listener,pos); }
		return chan;
	}
	void StopChannel(FISoundChannel *chan) { Voice *v=GetVoice(chan); if(v) EndVoice(*v); }
	void ChannelVolume(FISoundChannel *chan,float vol) { FAudioGuard g(voiceLock); Voice *v=GetVoice(chan); if(v) v->volume=vol; }
	void MarkStartTime(FISoundChannel *chan) { if(chan) chan->StartTime.AsOne=SDL_GetTicks(); }
	unsigned int GetPosition(FISoundChannel *chan) { FAudioGuard g(voiceLock); Voice *v=GetVoice(chan); return v ? (unsigned int)v->position : 0; }
	float GetAudibility(FISoundChannel *chan) { FAudioGuard g(voiceLock); Voice *v=GetVoice(chan); return v ? v->volume*sfxVolume*(v->left+v->right)*.5f : 0; }
	void Sync(bool value) { FAudioGuard g(voiceLock); syncing=value; }
	void SetSfxPaused(bool paused,int slot) { if(slot<0 || slot>=32)return; FAudioGuard g(voiceLock); if(paused) pauseSlots|=1u<<slot; else pauseSlots&=~(1u<<slot); }
	void SetInactive(EInactiveState state) {
		FAudioGuard g(voiceLock);
		inactive=state;
		if(device) { SDL_ClearQueuedAudio(device); SDL_PauseAudioDevice(device,state==INACTIVE_Complete); }
	}
	void UpdateSoundParams3D(SoundListener *listener,FISoundChannel *chan,bool,const FVector3 &pos,const FVector3 &) { FAudioGuard g(voiceLock); Voice *v=GetVoice(chan); if(v) Spatial(*v,listener,pos); }
	void UpdateListener(SoundListener *) {}
	// Game thread. With the audio thread running this only reaps finished
	// voices; without it, it also mixes, as the original backend did.
	void UpdateSounds() {
		if(!device) return;
#ifdef ZD_VITA
		Uint64 vitaAudioStart = I_VitaPerfClock();
#endif
		if(!mixThread) {
			bool idle;
			{ FAudioGuard g(voiceLock); idle=syncing || inactive==INACTIVE_Complete; }
			if(!idle) PumpAudio();
		}
		for(int i=0;i<VoiceCount;++i) {
			bool done;
			{ FAudioGuard g(voiceLock); done=voices[i].channel && voices[i].finished; }
			if(done) EndVoice(voices[i]);
		}
#ifdef ZD_VITA
		I_VitaPerfAdd(VPERF_AUDIO, vitaAudioStart);
#endif
	}
	void UpdateMusic() { UpdateSounds(); }
	void PrintStatus() { Printf("SDL2 audio: 44100 Hz stereo, 32 effect voices, queued PCM, mixer %s.\n", mixThread ? "on audio thread (core 1)" : "on game thread"); }
	void PrintDriversList() { Printf("SDL2 Vita audio driver.\n"); }
	void RemoveStream(PCMStream *stream) {
		FAudioGuard g(streamLock);
		for(size_t i=0;i<streams.size();++i) if(streams[i]==stream) { streams.erase(streams.begin()+i); break; }
	}
};
void PCMStream::Detach() { if(owner) { owner->RemoveStream(this); owner=NULL; lock=NULL; } }
PCMStream::~PCMStream() { Detach(); }
} // namespace

SoundRenderer *CreateSDLSoundRenderer() { return new SDLSoundRenderer; }
