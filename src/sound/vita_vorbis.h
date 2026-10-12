#ifndef ZDOOM_VITA_VORBIS_H
#define ZDOOM_VITA_VORBIS_H
#include <vector>
#include <cstddef>
// Decode mono/stereo Ogg Vorbis effects to the mixer's cached 16-bit PCM.
// No decoding is performed during playback.
bool VitaDecodeVorbis(const unsigned char *data, int length,
 std::vector<short> &pcm, int &rate, int &channels);
// Metadata-only conservative stereo PCM budget; rejects unknown lengths.
bool VitaVorbisPCMBytes(const unsigned char *data, int length, size_t &bytes);
#endif
