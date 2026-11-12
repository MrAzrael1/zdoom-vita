// Lightweight Vita frame profiling. Timings are accumulated, never logged per frame.
#ifndef ZDOOM_VITA_PERF_H
#define ZDOOM_VITA_PERF_H
#ifdef ZD_VITA
#include <SDL.h>
enum EVitaPerfStage { VPERF_GAME, VPERF_DISPLAY, VPERF_RENDER, VPERF_CONVERT, VPERF_PRESENT, VPERF_AUDIO, VPERF_THINKERS, VPERF_SPRITE_SORT, VPERF_SPRITE_DRAW, VPERF_PNG_LOAD, VPERF_SOUND_LOAD, VPERF_EFFECT_MIX, VPERF_SYNTH, VPERF_COUNT };
bool I_VitaPerfEnabled();
void I_VitaPerfAdd(EVitaPerfStage stage, Uint64 start);
void I_VitaPerfSprites(unsigned count);
void I_VitaPerfFrame(Uint64 start);
inline Uint64 I_VitaPerfClock() { return I_VitaPerfEnabled() ? SDL_GetPerformanceCounter() : 0; }
// Scope timings include early returns without per-asset log writes.
class FVitaPerfScope
{
	EVitaPerfStage stage;
	Uint64 start;
public:
	explicit FVitaPerfScope(EVitaPerfStage which) : stage(which), start(I_VitaPerfClock()) {}
	~FVitaPerfScope() { I_VitaPerfAdd(stage, start); }
};
#endif
#endif
