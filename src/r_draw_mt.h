// Multicore software-renderer drawing (Vita v0.13).
//
// While the 3D view is being rendered, the column/span/wall drawers in
// r_draw.cpp do not write pixels themselves. They capture their parameters
// into a command and append it to a queue. Worker threads (Vita user cores 1
// and 2) execute every command, but each thread only touches its own band of
// screen rows, so no two threads ever write the same pixel and per-pixel
// ordering (translucency, overdraw) is exactly that of the single-threaded
// renderer. The band boundary is re-balanced every frame from worker load.
//
// The game thread keeps doing visibility work (BSP, clipping, sprite sorting)
// and optionally owns a share of the rows itself (vita_drawmainshare).
//
// Anything that reads neighbouring pixels or writes the framebuffer outside
// the queued drawers (fuzz, sloped/colored planes, fog boundaries, voxels)
// calls R_DrawerSync() or uses FDrawerInlineScope first, which waits for all
// queued work and then draws directly on the game thread.
#ifndef __R_DRAW_MT_H__
#define __R_DRAW_MT_H__

#include "doomtype.h"

struct FColumnArgs
{
	BYTE *dest;
	int count, pitch;
	DWORD iscale, texturefrac;
	BYTE *colormap;
	const BYTE *source;
	int color;
	DWORD srccolor;
	DWORD *srcblend, *destblend;
	BYTE *translation;
	int bits;
};

struct FVline4Args
{
	BYTE *dest;
	int count, pitch, bits;
	DWORD *srcblend, *destblend;
	DWORD vplce[4], vince[4];
	BYTE *palookupoffse[4];
	const BYTE *bufplce[4];
};

struct FSpanArgs
{
	BYTE *dest; // ylookup[ds_y] + ds_x1 + dc_destorg, captured at submit time
	int x1, x2;
	BYTE *colormap;
	DWORD xfrac, yfrac, xstep, ystep;
	int xbits, ybits;
	const BYTE *source;
	int color;
	DWORD *srcblend, *destblend;
};

struct FRectArgs
{
	BYTE *dest;	// top-left pixel
	int ycount, countbase, spacing;
	DWORD fg;
	DWORD *bg2rgb;
};

enum EDrawerKind
{
	DK_COLUMN,	// FColumnArgs, rows row..row+count-1
	DK_VLINE4,	// FVline4Args, rows row..row+count-1, four adjacent columns
	DK_SPAN,	// FSpanArgs, single row
	DK_RECT,	// FRectArgs, rows row..row+ycount-1
};

typedef void (*FColumnKernel)(FColumnArgs &);
typedef void (*FVline4Kernel)(FVline4Args &);
typedef void (*FSpanKernel)(FSpanArgs &);
typedef void (*FRectKernel)(FRectArgs &);

// Submit helpers used by the r_draw.cpp drawer stubs. They capture the
// current dc_*/ds_* globals. When queueing is inactive they run the kernel
// immediately on the calling thread, which is identical to the old drawers.
void R_SubmitColumn(FColumnKernel kernel, int bits = 0);
DWORD R_SubmitVline1(FColumnKernel kernel, int bits);	// returns the final frac, like vlinec1
void R_SubmitVline4(FVline4Kernel kernel, int bits);	// advances vplce[] like vlinec4
void R_SubmitSpan(FSpanKernel kernel);
void R_SubmitRect(FRectKernel kernel, const FRectArgs &args, int firstrow);

// Banded commands carry an opaque argument block (copied into the queue) and
// a kernel that draws only rows [bandBegin, bandEnd). Used for whole sprites.
enum { DRAWER_BANDED_ARGS_MAX = 160 };
typedef void (*FBandedKernel)(const void *args, int bandBegin, int bandEnd);
void R_SubmitBanded(FBandedKernel kernel, const void *args, size_t size);

// Helpers for banded kernels and their submitters.
void R_CaptureColumnArgs(FColumnArgs &a);	// dc_* globals, bits = 0
// Draws column 'a' (first row 'row') clipped to rows [bandBegin, bandEnd).
void R_DrawColumnClipped(FColumnKernel kernel, const FColumnArgs &a, int row, int bandBegin, int bandEnd);
// The queueable kernel behind a column drawer such as colfunc, or NULL if that
// drawer cannot run on a worker (fuzz).
FColumnKernel R_GetColumnKernel(void (*drawer)(void));

// Frame window: the 3D view of R_RenderActorView.
void R_BeginDrawerQueue();
void R_EndDrawerQueue();
bool R_DrawerQueueActive();

// Waits until every queued command has been drawn. Afterwards the game thread
// may read or write any pixel until the next submit.
void R_DrawerSync();

// Syncs, then draws everything inline on the game thread until destroyed.
class FDrawerInlineScope
{
	bool saved;
public:
	FDrawerInlineScope();
	~FDrawerInlineScope();
};

// Memory that stays valid until the end of the current queue window (used for
// composited double-sky columns, which the old code kept in a 4-entry ring).
// Returns NULL when queueing is inactive; the caller then uses its own buffer.
BYTE *R_DrawerFrameAlloc(size_t bytes);
// Incremented whenever the frame arena is reset, so caches of arena pointers
// can tell that they are stale.
extern unsigned int DrawerFrameArenaGeneration;

// Scoped window, so an error thrown mid-render still flushes and closes it.
class FDrawerQueueWindow
{
public:
	FDrawerQueueWindow() { R_BeginDrawerQueue(); }
	~FDrawerQueueWindow() { R_EndDrawerQueue(); }
};

// Runs fn over rows [0, rows) split between the game thread and both workers,
// and returns when every part is done. Used outside the 3D view (e.g. for the
// final palette conversion). Runs fn(ctx, 0, rows) directly when threads are
// off or unavailable. Parts never overlap.
typedef void (*FParallelRowsJob)(void *ctx, int firstrow, int endrow);
void R_DrawerParallelRows(FParallelRowsJob fn, void *ctx, int rows);

// Profiling counters since the previous call (game thread only).
struct FDrawerStats
{
	unsigned frames, commands, syncs, busyMicros[2];
	bool active;
	int mainshare, worker1Percent;
};
void R_DrawerTakeStats(FDrawerStats &out);

#ifdef R_DRAW_MT_VERIFY
void R_DrawerForceSplitForTest(int split);	// worker 1 share in 1/1024ths, -1 = adaptive
#endif

void R_ShutdownDrawerThreads();

#endif
