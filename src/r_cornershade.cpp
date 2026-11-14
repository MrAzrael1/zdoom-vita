// Corner shading (fake ambient occlusion); see r_cornershade.h.

#include <string.h>
#include <math.h>
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

bool CornerShadeRecording;

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
		float planeStep, wallStep;	// fraction of the gradient per row
	};

	struct FCornerArgs
	{
		BYTE *destorg;
		int pitch;
		const FCornerSeam *seams;
		int count;
	};

	FCornerSeam Seams[MAX_SEAMS];
	int SeamCount;
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

	// Darkness level for a pixel 'd' rows from the seam: a quadratic falloff
	// that reaches zero at the end of the gradient.
	inline int Level(int d, float step)
	{
		const float t = 1.f - (d + 0.5f) * step;
		if (t <= 0.f) return 0;
		return int(t * t * NUM_LEVELS + 0.5f);
	}

	void ShadeRows(BYTE *column, int pitch, int y, int begin, int end, float step, int bandBegin, int bandEnd)
	{
		if (begin < bandBegin) begin = bandBegin;
		if (end > bandEnd) end = bandEnd;
		for (int row = begin; row < end; ++row)
		{
			const int d = row >= y ? row - y : y - 1 - row;
			const int level = Level(d, step);
			if (level > 0)
			{
				BYTE *p = column + row * pitch;
				*p = Tables[level][*p];
			}
		}
	}

	void K_CornerShade(const void *argp, int bandBegin, int bandEnd)
	{
		const FCornerArgs &a = *(const FCornerArgs *)argp;
		for (int i = 0; i < a.count; ++i)
		{
			const FCornerSeam &s = a.seams[i];
			BYTE *column = a.destorg + s.x;
			ShadeRows(column, a.pitch, s.y, s.planeBegin, s.planeEnd, s.planeStep, bandBegin, bandEnd);
			ShadeRows(column, a.pitch, s.y, s.wallBegin, s.wallEnd, s.wallStep, bandBegin, bandEnd);
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
	GradientUnits = float(int(vita_cornershade_size));
	InvFocalY = 65536.f / float(FocalLengthY);
	MaxRows = viewheight / 2 > 1 ? viewheight / 2 : 1;
}

void R_CornerShadeAdd(int x, float seamY, int wallLimit, int planeLimit, float height, float dy, bool ceiling)
{
	if (SeamCount >= MAX_SEAMS) return;
	if (height < 1.f || dy < 1.f) return;
	// The seam is between rows y-1 and y (y = first floor row / first wall row
	// below a ceiling), matching WallMost's truncation.
	if (seamY < -16384.f) seamY = -16384.f;
	else if (seamY > 16384.f) seamY = 16384.f;
	const int y = int(floorf(seamY));

	// A seam hidden behind nearer geometry (the bottom edge of an opening door,
	// the lip of a lowered lift) is skipped: shading the visible wall beside it
	// would show as a dark band through the opening. Only seams hidden by the
	// edge of the view still shade the wall next to them.
	if (ceiling ? (y <= planeLimit && planeLimit > 0) : (y >= planeLimit && planeLimit < viewheight)) return;

	// Rows per map unit: vertically on the wall at this depth, and in depth on
	// the floor/ceiling, which is foreshortened by dy / focal length.
	const float wallRows = dy / height * GradientUnits;
	const float planeRows = wallRows * dy * InvFocalY;
	if (wallRows < 1.f && planeRows < 1.f) return;
	const int wr = wallRows >= 1.f ? (wallRows < MaxRows ? int(wallRows) + 1 : MaxRows) : 0;
	const int pr = planeRows >= 1.f ? (planeRows < MaxRows ? int(planeRows) + 1 : MaxRows) : 0;

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
	if (planeBegin >= planeEnd) planeBegin = planeEnd = 0;
	if (wallBegin >= wallEnd) wallBegin = wallEnd = 0;
	if (planeBegin == planeEnd && wallBegin == wallEnd) return;

	FCornerSeam &s = Seams[SeamCount++];
	s.x = short(x);
	s.y = short(y);
	s.wallStep = wallRows >= 1.f ? 1.f / wallRows : 1.f;
	s.planeStep = planeRows >= 1.f ? 1.f / planeRows : 1.f;
	s.planeBegin = short(planeBegin);
	s.planeEnd = short(planeEnd);
	s.wallBegin = short(wallBegin);
	s.wallEnd = short(wallEnd);
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
	R_SubmitBanded(K_CornerShade, &args, sizeof(args));
}
