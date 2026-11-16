// Multicore software-renderer drawing. See r_draw_mt.h for the overview.
//
// Queue: a single-producer ring of drawing commands. The game thread is the
// producer. Each consumer (two workers, plus the game thread when it owns a
// share of rows) walks every command in order and draws only the part that
// falls inside its own band of screen rows. Bands are fixed for a window, so
// every pixel is written by exactly one thread, in submission order.
//
// Bands are contiguous: a column or wall slice is clipped to the band and drawn
// as one normal run. The boundary between the two worker bands is re-balanced
// after every window from how long each worker was busy, which compensates
// for uneven scenes (sky above, crowds below) and for the audio thread sharing
// core 1 with worker 1.
//
// Commands reference texture, colormap and translation memory by pointer. All
// of that stays valid for the frame; the only per-column scratch buffer the
// renderer reused (double-sky composites) now comes from R_DrawerFrameAlloc.
// The window is flushed before the 3D view ends, so camera textures are
// complete before the main view samples them and 2D drawing never overlaps.
//
// The same workers also run R_DrawerParallelRows jobs (the final palette
// conversion of each frame) outside the 3D window.
#include <SDL.h>
#include <SDL_thread.h>
#include <atomic>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "r_draw_mt.h"
#include "r_local.h"
#include "r_draw.h"
#include "c_cvars.h"
#include "i_system.h"
#ifdef ZD_VITA
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/cpu.h>
#include "posix/vita_debug.h"
#include "posix/vita_perf.h"
#else
#define I_VitaTrace(...) ((void)0)
#endif

CVAR(Bool, vita_drawthreads, true, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
// Eighths of the screen rows the game thread draws itself. -1 (default) tunes
// it automatically (AutoTuneShare); 0..4 fixes it (console only). 0 leaves
// all drawing to the two worker cores while the game thread does visibility
// work. Replaces the old vita_drawmainshare menu slider.
CUSTOM_CVAR(Int, vita_drawshare, -1, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
{
	if (self < -1) self = -1;
	else if (self > 4) self = 4;
}

namespace
{
enum { RING_SIZE = 8192, RING_MASK = RING_SIZE - 1, NUM_WORKERS = 2, NUM_CONSUMERS = NUM_WORKERS + 1 };
enum { ARENA_SIZE = 512*1024 };
// Commands are made visible to the workers in batches: one release store and
// one wake check per batch instead of per command.
enum { PUBLISH_BATCH = 32 };
// Worker 1's share of the worker rows, in 1/1024ths. Starts slightly below
// half because worker 1 shares core 1 with the audio thread.
enum { SPLIT_ONE = 1024, SPLIT_MIN = 32, SPLIT_MAX = SPLIT_ONE - 32, SPLIT_START = 460 };

struct FJobArgs
{
	FParallelRowsJob fn;
	void *ctx;
	int begin[NUM_CONSUMERS], end[NUM_CONSUMERS];
};

enum { DK_JOB = DK_RECT + 1, DK_BANDED };

struct FBandedArgs
{
	FBandedKernel fn;
	union { void *align; double align2; unsigned char data[DRAWER_BANDED_ARGS_MAX]; } u;
};

struct FDrawerCommand
{
	int kind;
	int row;
	union
	{
		FColumnKernel column;
		FVline4Kernel vline4;
		FSpanKernel span;
		FRectKernel rect;
	} kernel;
	union
	{
		FColumnArgs c;
		FVline4Args v;
		FSpanArgs s;
		FRectArgs r;
		FJobArgs j;
		FBandedArgs b;
	} a;
};

struct FBand
{
	int begin, end;	// rows [begin, end)
	bool Empty() const { return begin >= end; }
};

struct FConsumer
{
	FBand band;
	std::atomic<unsigned> readIndex;
	std::atomic<int> sleeping;
	std::atomic<unsigned> busyMicros;	// for the profiler, taken every report
	std::atomic<unsigned> windowMicros;	// drawing time in the current window, for balancing
	mutable std::atomic<unsigned> jobMicros;	// part of windowMicros spent on parallel-rows jobs
	SDL_sem *wake;
	SDL_Thread *thread;
	int index;
};

FDrawerCommand *Ring;
std::atomic<unsigned> WriteIndex;	// published commands (visible to workers)
unsigned LocalWrite;				// game thread: next slot to fill (>= WriteIndex)
unsigned RingLimit;					// game thread: LocalWrite may advance up to here without checking readers
int CachedPitch;
uint32_t PitchReciprocal;
std::atomic<int> WorkersRunning;
FConsumer Consumers[NUM_CONSUMERS];	// [0] = game thread
bool ThreadsStarted, ThreadsFailed;
bool WindowActive, InlineMode;
int WindowDepth;
int WorkerSplit = SPLIT_START;
int WindowWorkerRows;				// rows split between the workers in this window
BYTE *Arena;
size_t ArenaUsed;
unsigned StatCommands, StatSyncs, StatFrames;
#ifdef R_DRAW_MT_VERIFY
int ForcedSplit = -1;
#endif

// Game thread rows on top, then worker 1, then worker 2 (to the canvas end).
void AssignBands(int mainshare, int height)
{
	if (height < 1) height = 1;
	if (height > MAXHEIGHT) height = MAXHEIGHT;
	if (mainshare < 0) mainshare = 0;
	if (mainshare > 4) mainshare = 4;
	int mainrows = height * mainshare / 8;
	WindowWorkerRows = height - mainrows;
	int w1rows = int((long long)WindowWorkerRows * WorkerSplit / SPLIT_ONE);
	Consumers[0].band.begin = 0;
	Consumers[0].band.end = mainrows;
	Consumers[1].band.begin = mainrows;
	Consumers[1].band.end = mainrows + w1rows;
	Consumers[2].band.begin = mainrows + w1rows;
	Consumers[2].band.end = MAXHEIGHT;
}

// Automatic game-thread share. Each view window the game thread does its
// visibility work (BSP, walls, sprites) and also draws the top rows of the
// view; the two workers draw the rest. Over each sample of windows the tuner
// measures, directly:
//   V    the game thread's own non-drawing work (window - its rows - waits)
//   D    the total drawing work (game-thread rows plus both workers)
//   wait time the game thread spent waiting for the workers
// and moves the share one step when two samples in a row agree:
//  - up, when the game thread waited for longer than one more step of rows
//    would cost it (the workers are the bottleneck);
//  - down, when the game thread hardly waited (it is the bottleneck) and
//    handing a step of rows to the workers would still leave them done first.
// Unlike comparing frame times between trial shares, these measurements stay
// meaningful when the scene changes from one second to the next.
int AutoShare = 0;		// current automatic share (0..4)
int CurrentShare = 0;	// share used by the current window
Uint64 WindowStartTicks, WindowWaitTicks, WindowMainDrawTicks, WindowJobTicks;

struct FShareTuner
{
	enum { SAMPLE_WINDOWS = 30 };
	double window = 0, wait = 0, mainDraw = 0, workers = 0;
	unsigned count = 0;
	int lastVote = 0;
} Tuner;

void AutoTuneShare(double windowMicros, double waitMicros, double mainDrawMicros, double workerMicros)
{
	FShareTuner &t = Tuner;
	t.window += windowMicros;
	t.wait += waitMicros;
	t.mainDraw += mainDrawMicros;
	t.workers += workerMicros;
	if (++t.count < FShareTuner::SAMPLE_WINDOWS) return;
	const double D = t.mainDraw + t.workers;
	const double V = t.window - t.mainDraw - t.wait;
	const double window = t.window, wait = t.wait, mainDraw = t.mainDraw;
	t.window = t.wait = t.mainDraw = t.workers = 0;
	t.count = 0;
	if (D <= 0 || window <= 0) { t.lastVote = 0; return; }

	// How much work one share step (1/8 of the rows) is: the game thread's
	// rows (the top of the view) are often cheaper than average, so use
	// what they actually cost when it has some.
	double rowsPerWork = 1.0;
	if (AutoShare > 0 && mainDraw > 0)
	{
		rowsPerWork = (AutoShare / 8.0) / (mainDraw / D);
		if (rowsPerWork < 0.5) rowsPerWork = 0.5; else if (rowsPerWork > 2.0) rowsPerWork = 2.0;
	}
	const double step = D / 8.0 / rowsPerWork;
	// One more step costs the game thread `step` and saves each worker
	// step/2, so it only pays when the game thread waited longer than a step.
	// One fewer step pays while the game thread would still finish last:
	// (V + mainDraw - step) against (workers + step) / 2, with half a step of
	// margin so the two rules never flip-flop around the same point.
	const double gameThread = (V > 0 ? V : 0) + mainDraw, perWorker = (D - mainDraw) * 0.5;
	int vote = 0;
	if (AutoShare < 4 && wait > step) vote = 1;
	else if (AutoShare > 0 && wait < window * 0.02 && gameThread - (perWorker + step * 0.5) > step * 0.5) vote = -1;
	if (vote != 0 && vote == t.lastVote)
	{
		AutoShare += vote;
		t.lastVote = 0;	// the next step needs two fresh samples
	}
	else t.lastVote = vote;
}

// Moves the worker boundary halfway towards the split that would have made
// both workers finish together in the window that just ended.
void Rebalance()
{
	unsigned b1 = Consumers[1].windowMicros.exchange(0, std::memory_order_relaxed);
	unsigned b2 = Consumers[2].windowMicros.exchange(0, std::memory_order_relaxed);
	int rows1 = Consumers[1].band.end - Consumers[1].band.begin;
	int rows2 = WindowWorkerRows - rows1;
	if (b1 == 0 || b2 == 0 || b1 + b2 < 300 || rows1 <= 0 || rows2 <= 0) return;
	double speed1 = double(rows1) / b1, speed2 = double(rows2) / b2;
	int ideal = int(SPLIT_ONE * speed1 / (speed1 + speed2));
	int split = WorkerSplit + (ideal - WorkerSplit) / 2;
	WorkerSplit = split < SPLIT_MIN ? SPLIT_MIN : split > SPLIT_MAX ? SPLIT_MAX : split;
}

void ExecuteColumn(const FDrawerCommand &cmd, const FBand &band)
{
	R_DrawColumnClipped(cmd.kernel.column, cmd.a.c, cmd.row, band.begin, band.end);
}

void ExecuteVline4(const FDrawerCommand &cmd, const FBand &band)
{
	const FVline4Args &a = cmd.a.v;
	if (a.count <= 0) return;
	const int y0 = cmd.row, y1 = cmd.row + a.count;
	const int top = y0 < band.begin ? band.begin : y0;
	const int bottom = y1 > band.end ? band.end : y1;
	if (top >= bottom) return;	// no overlap (or an empty band): kernels always draw >= 1 row
	FVline4Args b = a;
	if (top != y0 || bottom != y1)
	{
		const int delta = top - y0;
		b.dest += delta * a.pitch;
		for (int z = 0; z < 4; ++z) b.vplce[z] += DWORD(delta) * a.vince[z];
		b.count = bottom - top;
	}
	cmd.kernel.vline4(b);
}

void ExecuteRect(const FDrawerCommand &cmd, const FBand &band)
{
	const FRectArgs &a = cmd.a.r;
	if (a.ycount <= 0 || a.countbase <= 0) return;
	const int y0 = cmd.row, y1 = cmd.row + a.ycount;
	const int top = y0 < band.begin ? band.begin : y0;
	const int bottom = y1 > band.end ? band.end : y1;
	if (top >= bottom) return;	// no overlap (or an empty band)
	FRectArgs b = a;
	b.dest += (top - y0) * (a.countbase + a.spacing);
	b.ycount = bottom - top;
	cmd.kernel.rect(b);
}

void Execute(const FDrawerCommand &cmd, const FConsumer &c)
{
	const FBand &band = c.band;
	switch (cmd.kind)
	{
	case DK_COLUMN: ExecuteColumn(cmd, band); break;
	case DK_VLINE4: ExecuteVline4(cmd, band); break;
	case DK_SPAN:
		if (cmd.row >= band.begin && cmd.row < band.end)
		{
			FSpanArgs b = cmd.a.s;
			cmd.kernel.span(b);
		}
		break;
	case DK_RECT: ExecuteRect(cmd, band); break;
	case DK_BANDED:
		if (!band.Empty()) cmd.a.b.fn(cmd.a.b.u.data, band.begin, band.end);
		break;
	case DK_JOB:
		// The game thread runs its part of a job directly.
		if (c.index != 0 && cmd.a.j.begin[c.index] < cmd.a.j.end[c.index])
		{
			const Uint64 start = SDL_GetPerformanceCounter();
			cmd.a.j.fn(cmd.a.j.ctx, cmd.a.j.begin[c.index], cmd.a.j.end[c.index]);
			const Uint64 ticks = SDL_GetPerformanceCounter() - start;
			c.jobMicros.fetch_add(unsigned(ticks * 1000000 / SDL_GetPerformanceFrequency()), std::memory_order_relaxed);
		}
		break;
	}
}

// Processes queued commands up to 'until' for one consumer.
void Drain(FConsumer &c, unsigned until)
{
	unsigned r = c.readIndex.load(std::memory_order_relaxed);
	if (c.index == 0 && c.band.Empty())
	{
		c.readIndex.store(until, std::memory_order_release);
		return;
	}
	while (r != until)
	{
		Execute(Ring[r & RING_MASK], c);
		++r;
		if ((r & 31) == 0) c.readIndex.store(r, std::memory_order_release);
	}
	c.readIndex.store(r, std::memory_order_release);
}

// The game thread drawing its own rows, timed for the share tuner.
void DrainMain(unsigned until)
{
	if (Consumers[0].band.Empty())
	{
		Drain(Consumers[0], until);	// just catches the read index up
		return;
	}
	const Uint64 start = SDL_GetPerformanceCounter();
	Drain(Consumers[0], until);
	WindowMainDrawTicks += SDL_GetPerformanceCounter() - start;
}

int WorkerMain(void *data)
{
	FConsumer &c = *static_cast<FConsumer *>(data);
#ifdef ZD_VITA
	int mask = c.index == 1 ? SCE_KERNEL_CPU_MASK_USER_1 : SCE_KERNEL_CPU_MASK_USER_2;
	int result = sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), mask);
	I_VitaTrace("[Vita] Draw worker %d started (core %d affinity result 0x%08X)\n", c.index, c.index, (unsigned)result);
#endif
	while (WorkersRunning.load(std::memory_order_acquire))
	{
		unsigned w = WriteIndex.load(std::memory_order_acquire);
		if (c.readIndex.load(std::memory_order_relaxed) != w)
		{
			Uint64 start = SDL_GetPerformanceCounter();
			Drain(c, w);
			Uint64 ticks = SDL_GetPerformanceCounter() - start;
			unsigned micros = unsigned(ticks * 1000000 / SDL_GetPerformanceFrequency());
			c.busyMicros.fetch_add(micros, std::memory_order_relaxed);
			c.windowMicros.fetch_add(micros, std::memory_order_relaxed);
			continue;
		}
		// Briefly spin: commands usually arrive in quick succession.
		bool found = false;
		for (int spin = 0; spin < 2000 && !found; ++spin)
		{
			found = WriteIndex.load(std::memory_order_acquire) != c.readIndex.load(std::memory_order_relaxed);
		}
		if (found) continue;
		// Sleep until the producer posts. sleeping/WriteIndex are seq_cst so
		// either we see the new command or the producer sees us sleeping.
		c.sleeping.store(1, std::memory_order_seq_cst);
		if (WriteIndex.load(std::memory_order_seq_cst) != c.readIndex.load(std::memory_order_relaxed) ||
			!WorkersRunning.load(std::memory_order_seq_cst))
		{
			if (c.sleeping.exchange(0, std::memory_order_seq_cst) == 0)
				SDL_SemWait(c.wake); // the producer already posted; consume it
			continue;
		}
		SDL_SemWait(c.wake);
	}
	return 0;
}

void WakeWorkers()
{
	for (int w = 1; w < NUM_CONSUMERS; ++w)
	{
		FConsumer &c = Consumers[w];
		if (c.sleeping.load(std::memory_order_seq_cst) && c.sleeping.exchange(0, std::memory_order_seq_cst))
			SDL_SemPost(c.wake);
	}
}

// Makes every filled slot visible to the workers.
void Publish()
{
	if (WriteIndex.load(std::memory_order_relaxed) == LocalWrite) return;
	WriteIndex.store(LocalWrite, std::memory_order_seq_cst);
	WakeWorkers();
}

bool StartThreads()
{
	if (ThreadsStarted) return true;
	if (ThreadsFailed) return false;
	Ring = new FDrawerCommand[RING_SIZE];
	Arena = new BYTE[ARENA_SIZE];
	WriteIndex.store(0);
	LocalWrite = 0;
	RingLimit = RING_SIZE;
	WorkersRunning.store(1);
	for (int i = 0; i < NUM_CONSUMERS; ++i)
	{
		FConsumer &c = Consumers[i];
		c.index = i;
		c.readIndex.store(0);
		c.sleeping.store(0);
		c.busyMicros.store(0);
		c.windowMicros.store(0);
		c.jobMicros.store(0);
		c.wake = NULL;
		c.thread = NULL;
	}
	AssignBands(0, MAXHEIGHT);
	bool ok = true;
	for (int i = 1; i < NUM_CONSUMERS && ok; ++i)
	{
		FConsumer &c = Consumers[i];
		c.wake = SDL_CreateSemaphore(0);
		if (c.wake != NULL)
		{
			char name[16];
			snprintf(name, sizeof(name), "zdoom-draw%d", i);
			c.thread = SDL_CreateThreadWithStackSize(WorkerMain, name, 64*1024, &c);
		}
		ok = c.wake != NULL && c.thread != NULL;
	}
	if (!ok)
	{
		I_VitaTrace("[Vita] Draw threads unavailable (%s); drawing on the game thread\n", SDL_GetError());
		ThreadsStarted = true;	// let shutdown join whatever started
		R_ShutdownDrawerThreads();
		ThreadsFailed = true;
		return false;
	}
	ThreadsStarted = true;
	atterm(R_ShutdownDrawerThreads);
	return true;
}

inline bool Queueing()
{
	return WindowActive && !InlineMode;
}

unsigned MinReadIndex(unsigned w)
{
	unsigned oldest = w;
	for (int i = 0; i < NUM_CONSUMERS; ++i)
	{
		// The game thread only consumes when it owns rows (Drain() catches it
		// up instantly otherwise), so it cannot hold the ring back.
		if (i == 0 && Consumers[0].band.Empty()) continue;
		unsigned r = Consumers[i].readIndex.load(std::memory_order_acquire);
		if (w - r > w - oldest) oldest = r;
	}
	return oldest;
}

void WaitForWorkers(unsigned target, bool countAsDrawWait)
{
	const Uint64 tunerWaitStart = SDL_GetPerformanceCounter();
#ifdef ZD_VITA
	Uint64 waitStart = countAsDrawWait ? I_VitaPerfClock() : 0;
#endif
	for (int w = 1; w < NUM_CONSUMERS; ++w)
	{
		int spins = 0;
		while (Consumers[w].readIndex.load(std::memory_order_acquire) != target)
		{
			if (++spins > 256) { SDL_Delay(0); spins = 0; }
		}
	}
#ifdef ZD_VITA
	I_VitaPerfAdd(VPERF_DRAW_WAIT, waitStart);
#endif
	WindowWaitTicks += SDL_GetPerformanceCounter() - tunerWaitStart;
	(void)countAsDrawWait;
}

// Called when LocalWrite reaches RingLimit: publishes, then waits until the
// slowest consumer has freed a slot. RingLimit is derived from read indices
// that only ever grow, so between calls it is a safe (conservative) bound.
void MakeRoom()
{
	Publish();
	RingLimit = MinReadIndex(LocalWrite) + RING_SIZE;
	if (LocalWrite != RingLimit) return;
	DrainMain(LocalWrite);	// the game thread's own share first
#ifdef ZD_VITA
	Uint64 waitStart = I_VitaPerfClock();
#endif
	const Uint64 tunerWaitStart = SDL_GetPerformanceCounter();
	int spins = 0;
	while ((RingLimit = MinReadIndex(LocalWrite) + RING_SIZE) == LocalWrite)
	{
		WakeWorkers();
		if (++spins > 256) { SDL_Delay(0); spins = 0; }
	}
	WindowWaitTicks += SDL_GetPerformanceCounter() - tunerWaitStart;
#ifdef ZD_VITA
	I_VitaPerfAdd(VPERF_DRAW_WAIT, waitStart);
#endif
}

inline FDrawerCommand &BeginCommand()
{
	if (LocalWrite == RingLimit) MakeRoom();
	return Ring[LocalWrite & RING_MASK];
}

inline void PublishCommand()
{
	++LocalWrite;
	++StatCommands;
	if (LocalWrite - WriteIndex.load(std::memory_order_relaxed) >= PUBLISH_BATCH) Publish();
}

// The Vita's Cortex-A9 has no integer divide instruction, so the row is found
// with a cached reciprocal multiply (exact after one correction step).
int RowOf(const BYTE *dest)
{
	const int pitch = dc_pitch;
	if (pitch <= 0 || dest < dc_destorg) return -1;
	const uint64_t offset = uint64_t(dest - dc_destorg);
	if (offset >= uint64_t(MAXHEIGHT) * uint64_t(pitch)) return -1;
	if (pitch != CachedPitch)
	{
		CachedPitch = pitch;
		PitchReciprocal = uint32_t(((uint64_t(1) << 32) + pitch - 1) / pitch);	// ceil(2^32 / pitch)
	}
	uint32_t row = uint32_t((offset * PitchReciprocal) >> 32);	// row or row + 1
	if (uint64_t(row) * uint64_t(pitch) > offset) --row;
#ifdef R_DRAW_MT_VERIFY
	if (row != offset / uint64_t(pitch)) { fprintf(stderr, "RowOf mismatch\n"); abort(); }
#endif
	return int(row);
}

void CaptureColumn(FColumnArgs &a, int bits)
{
	a.dest = dc_dest;
	a.count = dc_count;
	a.pitch = dc_pitch;
	a.iscale = dc_iscale;
	a.texturefrac = dc_texturefrac;
	a.colormap = dc_colormap;
	a.source = dc_source;
	a.color = dc_color;
	a.srccolor = dc_srccolor;
	a.srcblend = dc_srcblend;
	a.destblend = dc_destblend;
	a.translation = dc_translation;
	a.bits = bits;
}

void CaptureVline4(FVline4Args &a, int bits)
{
	a.dest = dc_dest;
	a.count = dc_count;
	a.pitch = dc_pitch;
	a.bits = bits;
	a.srcblend = dc_srcblend;
	a.destblend = dc_destblend;
	for (int z = 0; z < 4; ++z)
	{
		a.vplce[z] = vplce[z];
		a.vince[z] = vince[z];
		a.palookupoffse[z] = palookupoffse[z];
		a.bufplce[z] = bufplce[z];
	}
}

void CaptureSpan(FSpanArgs &a)
{
	a.dest = ylookup[ds_y] + ds_x1 + dc_destorg;
	a.x1 = ds_x1;
	a.x2 = ds_x2;
	a.colormap = ds_colormap;
	a.xfrac = ds_xfrac;
	a.yfrac = ds_yfrac;
	a.xstep = ds_xstep;
	a.ystep = ds_ystep;
	a.xbits = ds_xbits;
	a.ybits = ds_ybits;
	a.source = ds_source;
	a.color = ds_color;
	a.srcblend = dc_srcblend;
	a.destblend = dc_destblend;
}
} // namespace

unsigned int DrawerFrameArenaGeneration;

void R_DrawColumnClipped(FColumnKernel kernel, const FColumnArgs &a, int row, int bandBegin, int bandEnd)
{
	if (a.count <= 0) return;
	const int y0 = row, y1 = row + a.count;
	const int top = y0 < bandBegin ? bandBegin : y0;
	const int bottom = y1 > bandEnd ? bandEnd : y1;
	if (top >= bottom) return;	// no overlap (or an empty band): kernels always draw >= 1 row
	FColumnArgs b = a;
	if (top != y0 || bottom != y1)
	{
		const int delta = top - y0;
		b.dest += delta * a.pitch;
		b.texturefrac += DWORD(delta) * a.iscale;
		b.count = bottom - top;
	}
	kernel(b);
}

void R_CaptureColumnArgs(FColumnArgs &a)
{
	CaptureColumn(a, 0);
}

void R_SubmitBanded(FBandedKernel kernel, const void *args, size_t size)
{
	if (!Queueing() || size > DRAWER_BANDED_ARGS_MAX)
	{
		if (Queueing()) R_DrawerSync();
		kernel(args, 0, MAXHEIGHT);
		return;
	}
	FDrawerCommand &cmd = BeginCommand();
	cmd.kind = DK_BANDED;
	cmd.row = 0;
	cmd.a.b.fn = kernel;
	memcpy(cmd.a.b.u.data, args, size);
	PublishCommand();
}

void R_SubmitColumn(FColumnKernel kernel, int bits)
{
	int row;
	if (!Queueing() || dc_count <= 0 || (row = RowOf(dc_dest)) < 0)
	{
		if (Queueing()) R_DrawerSync();
		FColumnArgs a;
		CaptureColumn(a, bits);
		kernel(a);
		return;
	}
	FDrawerCommand &cmd = BeginCommand();
	cmd.kind = DK_COLUMN;
	cmd.row = row;
	cmd.kernel.column = kernel;
	CaptureColumn(cmd.a.c, bits);
	PublishCommand();
}

DWORD R_SubmitVline1(FColumnKernel kernel, int bits)
{
	// The wall code chains columns through the returned texture position, so
	// it is computed here instead of being read back from the kernel.
	DWORD result = DWORD(dc_texturefrac) + DWORD(dc_count) * DWORD(dc_iscale);
	R_SubmitColumn(kernel, bits);
	return result;
}

void R_SubmitVline4(FVline4Kernel kernel, int bits)
{
	int row;
	if (!Queueing() || dc_count <= 0 || (row = RowOf(dc_dest)) < 0)
	{
		if (Queueing()) R_DrawerSync();
		FVline4Args a;
		CaptureVline4(a, bits);
		kernel(a);
	}
	else
	{
		FDrawerCommand &cmd = BeginCommand();
		cmd.kind = DK_VLINE4;
		cmd.row = row;
		cmd.kernel.vline4 = kernel;
		CaptureVline4(cmd.a.v, bits);
		PublishCommand();
	}
	// vlinec4 leaves vplce[] advanced past the drawn rows for the caller.
	for (int z = 0; z < 4; ++z) vplce[z] += DWORD(dc_count) * vince[z];
}

void R_SubmitSpan(FSpanKernel kernel)
{
	if (!Queueing() || ds_y < 0 || ds_y >= MAXHEIGHT)
	{
		if (Queueing()) R_DrawerSync();
		FSpanArgs a;
		CaptureSpan(a);
		kernel(a);
		return;
	}
	FDrawerCommand &cmd = BeginCommand();
	cmd.kind = DK_SPAN;
	cmd.row = ds_y;
	cmd.kernel.span = kernel;
	CaptureSpan(cmd.a.s);
	PublishCommand();
}

void R_SubmitRect(FRectKernel kernel, const FRectArgs &args, int firstrow)
{
	if (!Queueing() || firstrow < 0 || firstrow >= MAXHEIGHT)
	{
		if (Queueing()) R_DrawerSync();
		FRectArgs a = args;
		kernel(a);
		return;
	}
	FDrawerCommand &cmd = BeginCommand();
	cmd.kind = DK_RECT;
	cmd.row = firstrow;
	cmd.kernel.rect = kernel;
	cmd.a.r = args;
	PublishCommand();
}

void R_BeginDrawerQueue()
{
	if (WindowDepth++ > 0)
	{
		R_DrawerSync();	// nested view: keep strict ordering with the outer one
		return;
	}
	if (!vita_drawthreads || !StartThreads()) return;
#ifdef R_DRAW_MT_VERIFY
	if (ForcedSplit >= 0) WorkerSplit = ForcedSplit;
#endif
	// Workers are idle (the previous window ended with a full sync and every
	// job was waited for), so bands can change here; they are published to
	// the workers by the first WriteIndex store.
	CurrentShare = vita_drawshare >= 0 ? int(vita_drawshare) : AutoShare;
	AssignBands(CurrentShare, viewheight);
	WindowStartTicks = SDL_GetPerformanceCounter();
	WindowWaitTicks = WindowMainDrawTicks = WindowJobTicks = 0;
	for (int w = 1; w < NUM_CONSUMERS; ++w) Consumers[w].windowMicros.store(0, std::memory_order_relaxed);
	// The game thread may not have consumed during the previous window (no
	// rows); everything before LocalWrite is already drawn by the workers.
	Consumers[0].readIndex.store(LocalWrite, std::memory_order_relaxed);
	RingLimit = MinReadIndex(LocalWrite) + RING_SIZE;
	ArenaUsed = 0;
	WindowActive = true;
	InlineMode = false;
	++StatFrames;
}

void R_EndDrawerQueue()
{
	if (WindowDepth > 0 && --WindowDepth > 0) return;
	if (!WindowActive) return;
	R_DrawerSync();
	const Uint64 end = SDL_GetPerformanceCounter();
	WindowActive = false;
	ArenaUsed = 0;
	++DrawerFrameArenaGeneration;
	const unsigned jobMicros = Consumers[1].jobMicros.exchange(0, std::memory_order_relaxed)
		+ Consumers[2].jobMicros.exchange(0, std::memory_order_relaxed);
	unsigned workerMicros = Consumers[1].windowMicros.load(std::memory_order_relaxed)
		+ Consumers[2].windowMicros.load(std::memory_order_relaxed);
	workerMicros = workerMicros > jobMicros ? workerMicros - jobMicros : 0;
	Rebalance();
	if (vita_drawshare < 0)
	{
		const double us = 1000000.0 / SDL_GetPerformanceFrequency();
		AutoTuneShare((end - WindowStartTicks - WindowJobTicks) * us, WindowWaitTicks * us, WindowMainDrawTicks * us, workerMicros);
	}
}

bool R_DrawerQueueActive()
{
	return Queueing();
}

void R_DrawerSync()
{
	if (!WindowActive) return;
	Publish();
	unsigned w = LocalWrite;
	DrainMain(w);
	bool pending = false;
	for (int i = 1; i < NUM_CONSUMERS; ++i)
	{
		if (Consumers[i].readIndex.load(std::memory_order_acquire) != w) pending = true;
	}
	if (!pending) return;
	++StatSyncs;
	WakeWorkers();
	WaitForWorkers(w, true);
}

void R_DrawerParallelRows(FParallelRowsJob fn, void *ctx, int rows)
{
	if (rows <= 0) return;
	if (!vita_drawthreads || rows < 3 * NUM_CONSUMERS || !StartThreads())
	{
		fn(ctx, 0, rows);
		return;
	}
	if (WindowActive) R_DrawerSync();
	// Jobs are split evenly whatever the share, so the share tuner leaves
	// them out of its measurements.
	const Uint64 jobStart = SDL_GetPerformanceCounter();
	const Uint64 savedWait = WindowWaitTicks;
	// Everyone is otherwise idle here: the game thread takes a third, the
	// workers split the rest with the current drawing balance.
	int mainrows = rows / 3;
	int rest = rows - mainrows;
	int w1rows = int((long long)rest * WorkerSplit / SPLIT_ONE);
	FDrawerCommand &cmd = BeginCommand();
	cmd.kind = DK_JOB;
	cmd.row = 0;
	cmd.a.j.fn = fn;
	cmd.a.j.ctx = ctx;
	cmd.a.j.begin[0] = 0;					cmd.a.j.end[0] = mainrows;
	cmd.a.j.begin[1] = mainrows;			cmd.a.j.end[1] = mainrows + w1rows;
	cmd.a.j.begin[2] = mainrows + w1rows;	cmd.a.j.end[2] = rows;
	++LocalWrite;
	Publish();
	fn(ctx, 0, mainrows);
	WaitForWorkers(LocalWrite, false);
	WindowWaitTicks = savedWait;
	WindowJobTicks += SDL_GetPerformanceCounter() - jobStart;
}

FDrawerInlineScope::FDrawerInlineScope()
{
	saved = InlineMode;
	R_DrawerSync();
	InlineMode = true;
}

FDrawerInlineScope::~FDrawerInlineScope()
{
	InlineMode = saved;
}

BYTE *R_DrawerFrameAlloc(size_t bytes)
{
	if (!WindowActive) return NULL;
	bytes = (bytes + 15) & ~size_t(15);
	if (bytes > ARENA_SIZE) return NULL;
	if (ArenaUsed + bytes > ARENA_SIZE)
	{
		// Everything queued so far may still read the arena; finish it first.
		R_DrawerSync();
		ArenaUsed = 0;
		++DrawerFrameArenaGeneration;
	}
	BYTE *p = Arena + ArenaUsed;
	ArenaUsed += bytes;
	return p;
}

void R_DrawerTakeStats(FDrawerStats &out)
{
	out.frames = StatFrames;
	out.commands = StatCommands;
	out.syncs = StatSyncs;
	StatFrames = StatCommands = StatSyncs = 0;
	for (int w = 0; w < NUM_WORKERS; ++w)
	{
		out.busyMicros[w] = ThreadsStarted && !ThreadsFailed ? Consumers[w + 1].busyMicros.exchange(0) : 0;
	}
	out.active = ThreadsStarted && !ThreadsFailed && vita_drawthreads;
	out.mainshare = CurrentShare;
	out.mainshareAuto = vita_drawshare < 0;
	out.worker1Percent = WorkerSplit * 100 / SPLIT_ONE;
}

#ifdef R_DRAW_MT_VERIFY
void R_DrawerForceSplitForTest(int split)
{
	ForcedSplit = split;
}
#endif

void R_ShutdownDrawerThreads()
{
	if (!ThreadsStarted || ThreadsFailed) return;
	if (WindowActive) R_DrawerSync();
	WorkersRunning.store(0, std::memory_order_seq_cst);
	for (int i = 1; i < NUM_CONSUMERS; ++i)
	{
		FConsumer &c = Consumers[i];
		if (c.wake) SDL_SemPost(c.wake);
		if (c.thread) SDL_WaitThread(c.thread, NULL);
		if (c.wake) SDL_DestroySemaphore(c.wake);
		c.thread = NULL;
		c.wake = NULL;
	}
	ThreadsFailed = true;	// do not restart during shutdown
}
