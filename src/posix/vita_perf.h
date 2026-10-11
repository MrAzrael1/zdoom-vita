// Lightweight Vita frame profiling. Timings are accumulated, never logged per frame.
#ifndef ZDOOM_VITA_PERF_H
#define ZDOOM_VITA_PERF_H
#ifdef ZD_VITA
#include <SDL.h>
enum EVitaPerfStage { VPERF_GAME, VPERF_DISPLAY, VPERF_RENDER, VPERF_CONVERT, VPERF_PRESENT, VPERF_AUDIO, VPERF_COUNT };
bool I_VitaPerfEnabled();
void I_VitaPerfAdd(EVitaPerfStage stage, Uint64 start);
void I_VitaPerfFrame(Uint64 start);
inline Uint64 I_VitaPerfClock() { return I_VitaPerfEnabled() ? SDL_GetPerformanceCounter() : 0; }
#endif
#endif
