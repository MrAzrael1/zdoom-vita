// Vita SDL2 audio backend. Mixing and synthesizer callbacks run on the game
// thread; SDL consumes queued PCM on its audio thread.
#include <SDL.h>
#include <vector>
#include <cmath>
#include <cstring>
#include "i_sound.h"
#include "files.h"
#include "posix/vita_debug.h"
#include "posix/vita_perf.h"

namespace {
const int OutputRate = 44100;
const int MixFrames = 512;
const int VoiceCount = 32;

struct Sample {
	std::vector<short> pcm;
	int rate, channels, loopStart, loopEnd;
	int Frames() const { return int(pcm.size()) / channels; }
};

class SDLSoundRenderer;
class PCMStream : public SoundStream {
public:
	SDLSoundRenderer *owner;
	~PCMStream();
	SoundStreamCallback callback;
	void *userdata;
	std::vector<BYTE> buffer;
	int flags, rate, channels, bytesPerSample, frames;
	double cursor;
	unsigned int playedFrames;
	float volume;
	bool active, paused, ended;
	PCMStream(SDLSoundRenderer *renderer, SoundStreamCallback cb, int bytes, int fmt, int hz, void *ud)
		: owner(renderer), callback(cb), userdata(ud), flags(fmt), rate(hz),
		  channels((fmt & Mono) ? 1 : 2),
		  bytesPerSample((fmt & Float) || (fmt & Bits32) ? 4 : (fmt & Bits8) ? 1 : 2),
		  cursor(0), playedFrames(0), volume(1), active(false), paused(false), ended(false)
	{
		frames = bytes / (channels * bytesPerSample);
		buffer.resize(frames * channels * bytesPerSample);
		cursor = frames;
	}
	bool Play(bool, float vol) { volume=vol; active=true; paused=false; ended=false; cursor=frames; playedFrames=0; return true; }
	void Stop() { active=false; ended=true; }
	void SetVolume(float vol) { volume=vol; }
	bool SetPaused(bool value) { paused=value; return true; }
	unsigned int GetPosition() { return playedFrames * 1000ULL / OutputRate; }
	bool IsEnded() { return ended; }
	float ReadValue(int index) {
		const BYTE *p = &buffer[index * bytesPerSample];
		if (flags & Float) { float value; memcpy(&value,p,4); return std::isfinite(value) ? value : 0; }
		if (bytesPerSample == 1) return (int(*p)-128)/128.f;
		if (bytesPerSample == 2) { short value; memcpy(&value,p,2); return value/32768.f; }
		int value; memcpy(&value,p,4); return value/2147483648.f;
	}
	bool ReadFrame(float &left, float &right) {
		if (!active || paused) return false;
		while (cursor >= frames) {
			double carry = cursor - frames;
			memset(buffer.data(),(flags & Bits8) ? 128 : 0,buffer.size());
			if (!callback(this, buffer.data(), int(buffer.size()), userdata)) { Stop(); return false; }
			cursor = carry;
		}
		int index = int(cursor);
		if (index >= frames) { Stop(); return false; }
		left = ReadValue(index*channels)*volume;
		right = channels == 2 ? ReadValue(index*channels+1)*volume : left;
		cursor += double(rate)/OutputRate;
		++playedFrames;
		return true;
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
	Voice *GetVoice(FISoundChannel *chan) {
		return chan ? static_cast<Voice*>(chan->SysChannel) : NULL;
	}
	void EndVoice(Voice &voice) {
		FISoundChannel *chan = voice.channel;
		if (chan) S_ChannelEnded(chan); // GetPosition must still see the final position.
		voice = Voice();
	}
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
public:
	SDLSoundRenderer() : device(0), sfxVolume(1), musicVolume(1), pauseSlots(0), inactive(INACTIVE_Active), syncing(false) {
		if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) { I_VitaTrace("[Vita] Audio init failed: %s\n", SDL_GetError()); return; }
		SDL_AudioSpec want, have;
		SDL_zero(want);
		want.freq=OutputRate; want.format=AUDIO_S16SYS; want.channels=2; want.samples=MixFrames;
		device = SDL_OpenAudioDevice(NULL,0,&want,&have,0);
		if (!device) { I_VitaTrace("[Vita] Audio open failed: %s\n", SDL_GetError()); return; }
		I_VitaTrace("[Vita] Audio: %s, %d Hz stereo, %d samples\n", SDL_GetCurrentAudioDriver(), have.freq, have.samples);
		SDL_PauseAudioDevice(device,0);
	}
	~SDLSoundRenderer() {
		if(device) SDL_CloseAudioDevice(device);
		for(size_t i=0;i<streams.size();++i) streams[i]->owner=NULL;
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
	}
	bool IsValid() { return device != 0; }
	void SetSfxVolume(float vol) { sfxVolume=vol; }
	void SetMusicVolume(float vol) { musicVolume=vol; }
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
		SoundHandle result={NULL};
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
		for(int i=0;i<VoiceCount;++i) if(voices[i].sample==sample && voices[i].channel) EndVoice(voices[i]);
		delete sample;
	}
	unsigned int GetMSLength(SoundHandle sound) { Sample *s=(Sample*)sound.data; return s ? s->Frames()*1000ULL/s->rate : 0; }
	unsigned int GetSampleLength(SoundHandle sound) { Sample *s=(Sample*)sound.data; return s ? s->Frames() : 0; }
	float GetOutputRate() { return OutputRate; }
	SoundStream *CreateStream(SoundStreamCallback cb,int bytes,int flags,int rate,void *ud) {
		int unit=((flags&SoundStream::Mono)?1:2)*((flags&(SoundStream::Float|SoundStream::Bits32))?4:(flags&SoundStream::Bits8)?1:2);
		if(!cb || bytes<unit || rate<=0 || rate>192000) return NULL;
		PCMStream *stream=new PCMStream(this,cb,bytes,flags,rate,ud);
		streams.push_back(stream);
		I_VitaTrace("[Vita] Audio: synth stream %d Hz, flags=%d\n",rate,flags);
		return stream;
	}
	// Callback streams handle the bundled MIDI/MUS synthesizers. Compressed
	// digital music is a separate milestone requiring a decoder library.
	SoundStream *OpenStream(FileReader *reader,int) { delete reader; return NULL; }
	FISoundChannel *StartSound(SoundHandle sound,float vol,int pitch,int flags,FISoundChannel *reuse) {
		if(!sound.data) return NULL;
		Voice *voice=NULL;
		for(int i=0;i<VoiceCount;++i) if(!voices[i].channel) { voice=&voices[i]; break; }
		if(!voice) return NULL;
		voice->sample=(Sample*)sound.data;
		voice->volume=vol; voice->flags=flags;
		voice->step=double(voice->sample->rate)/OutputRate * (pitch>0 ? pitch/128.0 : 1.0);
		voice->channel=reuse ? reuse : S_GetChannel(voice);
		voice->channel->SysChannel=voice;
		voice->channel->StartTime.AsOne=SDL_GetTicks();
		voice->channel->DistanceScale=1;
		voice->channel->DistanceSqr=0;
		voice->channel->ManualRolloff=false;
		return voice->channel;
	}
	FISoundChannel *StartSound3D(SoundHandle sound,SoundListener *listener,float vol,FRolloffInfo *rolloff,float scale,int pitch,int,const FVector3 &pos,const FVector3 &,int,int flags,FISoundChannel *reuse) {
		FISoundChannel *chan=StartSound(sound,vol,pitch,flags,reuse);
		if(chan) { chan->Rolloff=*rolloff; chan->DistanceScale=scale; Spatial(*GetVoice(chan),listener,pos); }
		return chan;
	}
	void StopChannel(FISoundChannel *chan) { Voice *v=GetVoice(chan); if(v) EndVoice(*v); }
	void ChannelVolume(FISoundChannel *chan,float vol) { Voice *v=GetVoice(chan); if(v) v->volume=vol; }
	void MarkStartTime(FISoundChannel *chan) { if(chan) chan->StartTime.AsOne=SDL_GetTicks(); }
	unsigned int GetPosition(FISoundChannel *chan) { Voice *v=GetVoice(chan); return v ? (unsigned int)v->position : 0; }
	float GetAudibility(FISoundChannel *chan) { Voice *v=GetVoice(chan); return v ? v->volume*sfxVolume*(v->left+v->right)*.5f : 0; }
	void Sync(bool value) { syncing=value; }
	void SetSfxPaused(bool paused,int slot) { if(slot<0 || slot>=32)return; if(paused) pauseSlots|=1u<<slot; else pauseSlots&=~(1u<<slot); }
	void SetInactive(EInactiveState state) { inactive=state; if(device) { SDL_ClearQueuedAudio(device); SDL_PauseAudioDevice(device,state==INACTIVE_Complete); } }
	void UpdateSoundParams3D(SoundListener *listener,FISoundChannel *chan,bool,const FVector3 &pos,const FVector3 &) { Voice *v=GetVoice(chan); if(v) Spatial(*v,listener,pos); }
	void UpdateListener(SoundListener *) {}
	void UpdateSounds() {
		if(!device || syncing || inactive==INACTIVE_Complete) return;
#ifdef ZD_VITA
		Uint64 vitaAudioStart = I_VitaPerfClock();
#endif
		// Top up at game-tick cadence, with a bounded queue (~70ms).
		for(int block=0;block<6 && SDL_GetQueuedAudioSize(device)<3072*4;++block) {
			float mix[MixFrames*2]={0};
			for(int v=0;v<VoiceCount;++v) {
				Voice &voice=voices[v];
				if(!voice.channel || voice.finished || (pauseSlots && !(voice.flags&SNDF_NOPAUSE)))continue;
				Sample &s=*voice.sample;
				for(int i=0;i<MixFrames;++i) {
					int end=(voice.flags&SNDF_LOOP)?s.loopEnd:s.Frames();
					if(voice.position>=end) {
						if(voice.flags&SNDF_LOOP) voice.position=s.loopStart+std::fmod(voice.position-end,double(end-s.loopStart));
						else { voice.finished=true; break; }
					}
					int frame=int(voice.position), index=frame*s.channels;
					float l=s.pcm[index]/32768.f;
					float r=s.channels==2 ? s.pcm[index+1]/32768.f : l;
					mix[i*2]+=l*voice.volume*voice.left*sfxVolume;
					mix[i*2+1]+=r*voice.volume*voice.right*sfxVolume;
					voice.position+=voice.step;
				}
			}
			for(size_t s=0;s<streams.size();++s) for(int i=0;i<MixFrames;++i) {
				float l=0,r=0;
				if(streams[s]->ReadFrame(l,r)) { mix[i*2]+=l*musicVolume; mix[i*2+1]+=r*musicVolume; }
			}
			short output[MixFrames*2];
			for(int i=0;i<MixFrames*2;++i) {
				float value=inactive==INACTIVE_Mute ? 0 : mix[i];
				if(value>1)value=1;
				if(value<-1)value=-1;
				output[i]=short(value*32767);
			}
			if(SDL_QueueAudio(device,output,sizeof(output))!=0) { I_VitaTrace("[Vita] Audio queue failed: %s\n",SDL_GetError()); break; }
		}
		for(int i=0;i<VoiceCount;++i) if(voices[i].finished) EndVoice(voices[i]);
#ifdef ZD_VITA
		I_VitaPerfAdd(VPERF_AUDIO, vitaAudioStart);
#endif
	}
	void UpdateMusic() { UpdateSounds(); }
	void PrintStatus() { Printf("SDL2 audio: 44100 Hz stereo, 32 effect voices, queued PCM.\n"); }
	void PrintDriversList() { Printf("SDL2 Vita audio driver.\n"); }
	void RemoveStream(PCMStream *stream) {
		for(size_t i=0;i<streams.size();++i) if(streams[i]==stream) { streams.erase(streams.begin()+i); break; }
	}
};
PCMStream::~PCMStream() { if(owner) owner->RemoveStream(this); }
} // namespace

SoundRenderer *CreateSDLSoundRenderer() { return new SDLSoundRenderer; }
