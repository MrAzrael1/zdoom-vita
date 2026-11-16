// Corner shading (fake ambient occlusion); see r_cornershade.h.

#include <string.h>
#include <math.h>
#include <atomic>
#include <SDL.h>
#include "doomtype.h"
#include "templates.h"
#include "c_cvars.h"
#include "r_local.h"
#include "v_palette.h"
#include "colormatcher.h"
#include "r_draw_mt.h"
#include "r_cornershade.h"

// Off by default: an optional look rather than part of the original renderer.
CVAR(Bool, vita_cornershade, false, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
// Darkness right at the seam, in steps of 7%: 1 = 7%, 6 = 42%, 10 = 70%.
CUSTOM_CVAR(Int, vita_cornershade_strength, 6, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
{
	if (self < 1) self = 1;
	else if (self > 10) self = 10;
}
// Pixels at least this bright (percent luminance, as drawn) are never darkened,
// so light fixtures and other bright texture detail keep their glow.
// 100 shades everything.
CUSTOM_CVAR(Int, vita_cornershade_highlight, 80, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
{
	if (self < 10) self = 10;
	else if (self > 100) self = 100;
}
// Length of the gradient in map units (a player is 56 tall).
CUSTOM_CVAR(Int, vita_cornershade_size, 16, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
{
	if (self < 4) self = 4;
	else if (self > 64) self = 64;
}

// Diagnostic: paints floor-seam shading red and ceiling-seam shading green,
// so a screenshot shows exactly which seams shade which pixels. Console only.
CVAR(Bool, vita_cornershade_debug, false, 0)

bool CornerShadeRecording;
float CornerShadeMinRowsPerUnit;

namespace
{
	enum
	{
		NUM_LEVELS = 8,		// darkness steps; level 0 leaves the pixel alone
		MAX_SEAMS = 16384,	// a full screen of columns with several seams each
	};

	struct FCornerSeam
	{
		short x, y;
		short planeBegin, planeEnd;	// rows on the floor/ceiling side
		short wallBegin, wallEnd;	// rows on the wall side
		unsigned planeStep, wallStep;	// gradient per row, 1.0 = GRADIENT_ONE
		bool ceiling;
	};

	enum
	{
		GRADIENT_SHIFT = 16,
		GRADIENT_ONE = 1 << GRADIENT_SHIFT,	// position along the gradient, fixed point
		LUT_SHIFT = 6,						// GRADIENT_ONE >> LUT_SHIFT entries
		LUT_SIZE = GRADIENT_ONE >> LUT_SHIFT,
	};
	// Darkness level at each position along the gradient: a quadratic falloff,
	// round((1 - u)^2 * NUM_LEVELS), which reaches 0 at u = 0.75. Integer
	// stepping through this table replaces float maths per pixel.
	BYTE LevelLUT[LUT_SIZE];
	const float LEVEL_END = 0.75f;	// gradient fraction where the level reaches 0

	struct FCornerArgs
	{
		BYTE *destorg;
		int pitch;
		const FCornerSeam *seams;
		int count;
	};

	FCornerSeam Seams[MAX_SEAMS];
	int SeamCount;
	bool DebugColors;
	BYTE DebugFloor, DebugCeiling;
	float GradientUnits;
	float InvFocalY;
	int MaxRows;

	// Tables[k][c]: palette index c darkened by level k (1..NUM_LEVELS).
	BYTE Tables[NUM_LEVELS + 1][256];
	int TablesStrength = -1, TablesHighlight = -1;
	DWORD TablesPaletteHash;

	DWORD PaletteHash()
	{
		DWORD hash = 2166136261u;
		for (int i = 0; i < 256; ++i)
			hash = (hash ^ DWORD(GPalette.BaseColors[i])) * 16777619u;
		return hash;
	}

	// Only called at the start of a view, when no queued pass can be reading
	// the tables (every queue window ends with a full sync).
	void UpdateTables()
	{
		const int strength = vita_cornershade_strength;
		const int highlight = vita_cornershade_highlight;
		const DWORD hash = PaletteHash();
		if (strength == TablesStrength && highlight == TablesHighlight && hash == TablesPaletteHash) return;
		TablesStrength = strength;
		TablesHighlight = highlight;
		TablesPaletteHash = hash;
		const float maxDark = strength * 0.07f;
		for (int c = 0; c < 256; ++c) Tables[0][c] = BYTE(c);
		for (int k = 1; k <= NUM_LEVELS; ++k)
		{
			const float f = 1.f - maxDark * k / NUM_LEVELS;
			for (int c = 0; c < 256; ++c)
			{
				const PalEntry p = GPalette.BaseColors[c];
				// Rec. 601 luminance, 0..255.
				const int luma = (p.r * 299 + p.g * 587 + p.b * 114) / 1000;
				if (highlight < 100 && luma * 100 >= highlight * 255)
					Tables[k][c] = BYTE(c);
				else
					Tables[k][c] = ColorMatcher.Pick(int(p.r * f + 0.5f), int(p.g * f + 0.5f), int(p.b * f + 0.5f));
			}
		}
	}

	void BuildLevelLUT()
	{
		for (int i = 0; i < LUT_SIZE; ++i)
		{
			const float t = 1.f - (i + 0.5f) / LUT_SIZE;
			LevelLUT[i] = BYTE(int(t * t * NUM_LEVELS + 0.5f));
		}
	}

	// Shades rows [begin, end) of one column, walking away from the seam so
	// the loop can stop as soon as the gradient fades out. Rows at or below
	// the seam (begin >= y) are 'row - y' rows from it; rows above it are
	// 'y - 1 - row' rows from it and are walked upwards.
	void ShadeRows(BYTE *column, int pitch, int y, int begin, int end, unsigned step, int bandBegin, int bandEnd, int debugColor)
	{
		if (begin < bandBegin) begin = bandBegin;
		if (end > bandEnd) end = bandEnd;
		if (begin >= end) return;
		int count = end - begin;
		BYTE *p;
		int advance;
		unsigned u;
		if (begin >= y)
		{
			p = column + begin * pitch;
			advance = pitch;
			u = unsigned(begin - y) * step + (step >> 1);
		}
		else
		{
			p = column + (end - 1) * pitch;
			advance = -pitch;
			u = unsigned(y - end) * step + (step >> 1);
		}
		if (debugColor >= 0)
		{
			for (; count > 0 && u < GRADIENT_ONE && LevelLUT[u >> LUT_SHIFT]; --count, p += advance, u += step)
				*p = BYTE(debugColor);
			return;
		}
		for (; count > 0 && u < GRADIENT_ONE; --count, p += advance, u += step)
		{
			const int level = LevelLUT[u >> LUT_SHIFT];
			if (level == 0) break;
			*p = Tables[level][*p];
		}
	}

	// Profiling: game-thread recording time and worker kernel time.
	std::atomic<Uint64> KernelTicks;
	Uint64 RecordTicks;
	unsigned StatSeams, StatViews;

	void K_CornerShade(const void *argp, int bandBegin, int bandEnd)
	{
		const Uint64 start = SDL_GetPerformanceCounter();
		struct FTimer { Uint64 s; ~FTimer() { KernelTicks.fetch_add(SDL_GetPerformanceCounter() - s, std::memory_order_relaxed); } } timer = { start };
		const FCornerArgs &a = *(const FCornerArgs *)argp;
		for (int i = 0; i < a.count; ++i)
		{
			const FCornerSeam &s = a.seams[i];
			BYTE *column = a.destorg + s.x;
			const int debugColor = DebugColors ? (s.ceiling ? DebugCeiling : DebugFloor) : -1;
			ShadeRows(column, a.pitch, s.y, s.planeBegin, s.planeEnd, s.planeStep, bandBegin, bandEnd, debugColor);
			ShadeRows(column, a.pitch, s.y, s.wallBegin, s.wallEnd, s.wallStep, bandBegin, bandEnd, debugColor);
		}
	}
}

void R_CornerShadeBeginView()
{
	SeamCount = 0;
	// Special colormaps (invulnerability etc.) recolour the whole view; the
	// darkening tables assume ordinary palette colours.
	CornerShadeRecording = vita_cornershade && fixedcolormap == NULL && FocalLengthY > 0;
	if (!CornerShadeRecording) return;
	UpdateTables();
	if (LevelLUT[0] == 0) BuildLevelLUT();
	DebugColors = vita_cornershade_debug;
	if (DebugColors)
	{
		DebugFloor = ColorMatcher.Pick(255, 0, 0);
		DebugCeiling = ColorMatcher.Pick(0, 255, 0);
	}
	GradientUnits = float(int(vita_cornershade_size));
	InvFocalY = 65536.f / float(FocalLengthY);
	MaxRows = viewheight / 2 > 1 ? viewheight / 2 : 1;
	// R_CornerShadeAdd drops a seam when wall rows (rowsPerUnit * size) and
	// floor rows (wall rows * dy / focal length, dy at most the view height)
	// are both under one. Below this rows-per-unit both always are.
	{
		float planeFactor = viewheight * InvFocalY;
		if (planeFactor < 1.f) planeFactor = 1.f;
		CornerShadeMinRowsPerUnit = 1.f / (GradientUnits * planeFactor);
	}
}

void R_CornerShadeAdd(int x, float seamY, int wallLimit, int planeLimit, int oppositeLimit, float rowsPerUnit, float dy, bool ceiling)
{
	if (SeamCount >= MAX_SEAMS) return;
	if (dy < 1.f) return;
	// The seam is between rows y-1 and y (y = first floor row / first wall row
	// below a ceiling), matching WallMost's truncation.
	if (seamY < -16384.f) seamY = -16384.f;
	else if (seamY > 16384.f) seamY = 16384.f;
	const int y = int(floorf(seamY));

	// Rows [clipTop, clipBottom) of this column were still uncovered when the
	// seam was recorded; everything outside is nearer geometry drawn earlier.
	const int clipTop = ceiling ? planeLimit : oppositeLimit;
	const int clipBottom = ceiling ? oppositeLimit : planeLimit;

	// A seam hidden behind nearer geometry is skipped, whether that geometry is
	// above it (an opening door covering the floor of the room behind) or below
	// it (the lip of a lowered lift): shading next to it would show as dark
	// lines on that geometry or through the opening. Only seams hidden by the
	// edge of the view still shade the visible rows next to them.
	if (y < clipTop && clipTop > 0) return;
	if (y > clipBottom && clipBottom < viewheight) return;
	if (ceiling ? (y <= planeLimit && planeLimit > 0) : (y >= planeLimit && planeLimit < viewheight)) return;

	// Rows per map unit: vertically on the wall at this depth, and in depth on
	// the floor/ceiling, which is foreshortened by dy / focal length.
	const float wallRows = rowsPerUnit * GradientUnits;
	const float planeRows = wallRows * dy * InvFocalY;
	if (wallRows < 1.f && planeRows < 1.f) return;
	// Only the first 75% of the gradient has a non-zero level.
	const float wallLen = wallRows * LEVEL_END, planeLen = planeRows * LEVEL_END;
	const int wr = wallRows >= 1.f ? (wallLen < MaxRows ? int(wallLen) + 1 : MaxRows) : 0;
	const int pr = planeRows >= 1.f ? (planeLen < MaxRows ? int(planeLen) + 1 : MaxRows) : 0;

	// Visible rows only. A seam hidden off screen or behind nearer geometry
	// still shades the visible wall next to it; its floor/ceiling side is then
	// empty.
	int planeBegin, planeEnd, wallBegin, wallEnd;
	if (ceiling)
	{
		planeBegin = MAX(planeLimit, y - pr);
		planeEnd = y;
		wallBegin = MAX(y, planeLimit);
		wallEnd = MIN(wallLimit, y + wr);
	}
	else
	{
		wallBegin = MAX(wallLimit, y - wr);
		wallEnd = MIN(y, planeLimit);
		planeBegin = y;
		planeEnd = MIN(planeLimit, y + pr);
	}
	// Never shade outside the rows that were uncovered for this column.
	planeBegin = MAX(planeBegin, clipTop);
	planeEnd = MIN(planeEnd, clipBottom);
	wallBegin = MAX(wallBegin, clipTop);
	wallEnd = MIN(wallEnd, clipBottom);
	if (planeBegin >= planeEnd) planeBegin = planeEnd = 0;
	if (wallBegin >= wallEnd) wallBegin = wallEnd = 0;
	if (planeBegin == planeEnd && wallBegin == wallEnd) return;

	FCornerSeam &s = Seams[SeamCount++];
	s.x = short(x);
	s.y = short(y);
	s.wallStep = wallRows >= 1.f ? unsigned(float(GRADIENT_ONE) / wallRows) : GRADIENT_ONE;
	s.planeStep = planeRows >= 1.f ? unsigned(float(GRADIENT_ONE) / planeRows) : GRADIENT_ONE;
	s.planeBegin = short(planeBegin);
	s.planeEnd = short(planeEnd);
	s.wallBegin = short(wallBegin);
	s.wallEnd = short(wallEnd);
	s.ceiling = ceiling;
}

void R_CornerShadeApply()
{
	if (!CornerShadeRecording) return;
	CornerShadeRecording = false;
	if (SeamCount == 0) return;
	FCornerArgs args;
	args.destorg = dc_destorg;
	args.pitch = dc_pitch;
	args.seams = Seams;
	args.count = SeamCount;
	StatSeams += SeamCount;
	++StatViews;
	R_SubmitBanded(K_CornerShade, &args, sizeof(args));
}

void R_CornerShadeAddRecordTicks(unsigned long long ticks)
{
	RecordTicks += ticks;
}

bool R_CornerShadeTakeStats(double &recordMs, double &kernelMs, unsigned &seams, unsigned &views)
{
	const double ms = 1000.0 / SDL_GetPerformanceFrequency();
	recordMs = RecordTicks * ms;
	kernelMs = KernelTicks.exchange(0) * ms;
	seams = StatSeams;
	views = StatViews;
	RecordTicks = 0;
	StatSeams = StatViews = 0;
	return views != 0;
}
