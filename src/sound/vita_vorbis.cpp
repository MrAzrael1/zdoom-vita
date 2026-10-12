#include "vita_vorbis.h"
#include <cstring>
#include <new>
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_MAX_CHANNELS 2
#include "../thirdparty/stb_vorbis.c"

bool VitaDecodeVorbis(const unsigned char *data, int length,
 std::vector<short> &pcm, int &rate, int &channels)
{
 if (!data || length < 4 || memcmp(data, "OggS", 4) != 0) return false;
 int error=0;
 stb_vorbis *decoder=stb_vorbis_open_memory(data,length,&error,NULL);
 if (!decoder) return false;
 struct CloseDecoder {
  stb_vorbis *decoder;
  ~CloseDecoder() { stb_vorbis_close(decoder); }
 } close={decoder};
 stb_vorbis_info info=stb_vorbis_get_info(decoder);
 if ((info.channels!=1 && info.channels!=2) ||
     !info.sample_rate || info.sample_rate>192000) return false;
 // Limit a single cached effect to 32 MiB. Decode incrementally rather than
 // trusting the file's declared size or allocating a second full PCM copy.
 const size_t MaxSamples=16*1024*1024;
 std::vector<short> decoded;
 try {
  unsigned int frames=stb_vorbis_stream_length_in_samples(decoder);
  if (frames && size_t(frames)>MaxSamples/info.channels) return false;
  if (frames) decoded.reserve(size_t(frames)*info.channels);
  short block[4096];
  for (;;) {
   int got=stb_vorbis_get_samples_short_interleaved(decoder,info.channels,block,4096);
   if (got<=0) break;
   size_t samples=size_t(got)*info.channels;
   if (decoded.size()>MaxSamples-samples) return false;
   decoded.insert(decoded.end(),block,block+samples);
  }
 } catch (const std::bad_alloc &) { return false; }
 if (decoded.empty() || stb_vorbis_get_error(decoder)!=VORBIS__no_error) return false;
 pcm.swap(decoded);rate=int(info.sample_rate);channels=info.channels;
 return true;
}

bool VitaVorbisPCMBytes(const unsigned char *data, int length, size_t &bytes)
{
 bytes=0;
 if (!data || length<4 || memcmp(data,"OggS",4)) return false;
 int error=0;
 stb_vorbis *decoder=stb_vorbis_open_memory(data,length,&error,NULL);
 if (!decoder) return false;
 stb_vorbis_info info=stb_vorbis_get_info(decoder);
 unsigned frames=stb_vorbis_stream_length_in_samples(decoder);
 bool valid=(info.channels==1 || info.channels==2) && info.sample_rate &&
  info.sample_rate<=192000 && frames && size_t(frames)<=16*1024*1024/info.channels &&
  stb_vorbis_get_error(decoder)==VORBIS__no_error;
 if (valid) bytes=size_t(frames)*4; // Reserve stereo-sized PCM even for mono effects.
 stb_vorbis_close(decoder);
 return valid;
}
