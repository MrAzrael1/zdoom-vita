// Lightweight Vita frame profiling. Timings are accumulated, never logged per frame.
#ifndef ZDOOM_VITA_PERF_H
#define ZDOOM_VITA_PERF_H
#ifdef ZD_VITA
#include <SDL.h>
enum EVitaPerfStage { VPERF_GAME, VPERF_DISPLAY, VPERF_RENDER, VPERF_CONVERT, VPERF_PRESENT, VPERF_AUDIO, VPERF_THINKERS, VPERF_SPRITE_SORT, VPERF_SPRITE_DRAW, VPERF_PNG_LOAD, VPERF_SOUND_LOAD, VPERF_EFFECT_MIX, VPERF_SYNTH, VPERF_DRAW_WAIT, VPERF_R_WALLS, VPERF_R_PLANES, VPERF_R_MASKED, VPERF_COUNT };
bool I_VitaPerfEnabled();
void I_VitaPerfAdd(EVitaPerfStage stage, Uint64 start);
void I_VitaPerfSprites(unsigned count);
void I_VitaPerfFrame(Uint64 start);
// Map-load profiling: always on, one summary per load (see I_VitaLoadEnd).
// Sequential stages are timed with marks (each mark charges the time since
// the previous one); file reads, pk3 unpacking and asset decoding accumulate
// separately and overlap the stages.
enum EVitaLoadStage
{
	VLOAD_PREP,			// G_DoLoadLevel before P_SetupLevel
	VLOAD_MUSIC,		// S_Start: stopping sounds, starting the map's music
	VLOAD_MAPDATA,		// freeing the old map, opening and reading the map's lumps
	VLOAD_SCRIPTS,		// ACS (BEHAVIOR, LOADACS libraries), translators, dialogues
	VLOAD_NODES,		// nodes/segs/subsectors (or node building)
	VLOAD_BLOCKMAP,		// blockmap, reject, line grouping, zones
	VLOAD_THINGS,		// spawning map things
	VLOAD_SPECIALS,		// specials, polyobjects, player starts
	VLOAD_TEXTURES,		// precache of the map's own textures and sprites
	VLOAD_EFFECT_SPRITES,	// Vita effect-sprite preload
	VLOAD_SOUNDS,		// precache of the map's own sounds
	VLOAD_EFFECT_SOUNDS,	// Vita effect-sound preload
	VLOAD_FINISH,		// rest of P_SetupLevel and G_DoLoadLevel (snapshots, scripts)
	VLOAD_FILE_READ,	// overlapping: FileReader reads from the memory card
	VLOAD_ZIP,			// overlapping: pk3 lump unpacking, including its reads
	VLOAD_PNG,			// overlapping: PNG decoding
	VLOAD_SOUND_DECODE,	// overlapping: sound decoding
	VLOAD_COUNT
};
extern bool VitaLoadActive;
void I_VitaLoadBegin();
void I_VitaLoadMark(EVitaLoadStage stage);
void I_VitaLoadAdd(EVitaLoadStage stage, Uint64 start);
void I_VitaLoadEnd(const char *mapname);
unsigned I_VitaPNGDecodeCount();	// PNG textures decoded since startup
inline Uint64 I_VitaLoadClock() { return VitaLoadActive ? SDL_GetPerformanceCounter() : 0; }
class FVitaLoadScope
{
	EVitaLoadStage stage;
	Uint64 start;
public:
	explicit FVitaLoadScope(EVitaLoadStage which) : stage(which), start(I_VitaLoadClock()) {}
	~FVitaLoadScope() { if (start) I_VitaLoadAdd(stage, start); }
};
inline Uint64 I_VitaPerfClock() { return (I_VitaPerfEnabled() || VitaLoadActive) ? SDL_GetPerformanceCounter() : 0; }
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
