// Parallel asset decoding for map loads (Vita); see vita_loadpool.h.
#ifdef ZD_VITA

#include <atomic>
#include <SDL.h>
#include <psp2/kernel/threadmgr.h>
#include "vita_loadpool.h"
#include "vita_debug.h"

namespace
{
	enum
	{
		NUM_LOADERS = 2,
		// Input read but not yet decoded is capped so a long queue of large
		// lumps cannot spike memory; the game thread decodes instead of
		// reading further while over the cap.
		MAX_IN_FLIGHT = 24 * 1024 * 1024,
	};

	struct FRun
	{
		FVitaLoadJob **jobs;
		unsigned count;
		std::atomic<unsigned> ready, next, done;
		std::atomic<size_t> inFlight;
		size_t *sizes;
		std::atomic<Uint64> decodeTicks;
	};

	SDL_sem *WorkSem;
	SDL_Thread *Loaders[NUM_LOADERS];
	std::atomic<FRun *> Current;
	// Loaders inside a run. A loader counts itself in before reading Current,
	// so once the game thread has cleared Current and seen this reach zero, no
	// loader can still hold a pointer to the finished run.
	std::atomic<int> Busy;
	bool Started, StartFailed;

	// Claims the next job whose input has been read, or returns false.
	bool Claim(FRun &run, unsigned &index)
	{
		unsigned i = run.next.load(std::memory_order_relaxed);
		while (i < run.ready.load(std::memory_order_acquire))
		{
			if (run.next.compare_exchange_weak(i, i + 1, std::memory_order_acq_rel)) { index = i; return true; }
		}
		return false;
	}

	void DecodeOne(FRun &run, unsigned index)
	{
		const Uint64 start = SDL_GetPerformanceCounter();
		run.jobs[index]->Decode();
		run.decodeTicks.fetch_add(SDL_GetPerformanceCounter() - start, std::memory_order_relaxed);
		run.inFlight.fetch_sub(run.sizes[index], std::memory_order_relaxed);
		run.done.fetch_add(1, std::memory_order_release);
	}

	int LoaderMain(void *arg)
	{
		const int core = int(intptr_t(arg));
		sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(),
			core == 1 ? SCE_KERNEL_CPU_MASK_USER_1 : SCE_KERNEL_CPU_MASK_USER_2);
		for (;;)
		{
			SDL_SemWait(WorkSem);
			Busy.fetch_add(1);
			FRun *run = Current.load();
			if (run != NULL)
			{
				unsigned index;
				while (Claim(*run, index)) DecodeOne(*run, index);
			}
			Busy.fetch_sub(1);
		}
		return 0;
	}

	bool StartLoaders()
	{
		if (Started) return true;
		if (StartFailed) return false;
		WorkSem = SDL_CreateSemaphore(0);
		if (WorkSem == NULL) { StartFailed = true; return false; }
		for (int i = 0; i < NUM_LOADERS; ++i)
		{
			char name[16];
			SDL_snprintf(name, sizeof(name), "ZDLoader%d", i + 1);
			// stb_vorbis decodes with alloca-sized temporaries: give it room.
			Loaders[i] = SDL_CreateThreadWithStackSize(LoaderMain, name, 512*1024, (void *)intptr_t(i + 1));
			if (Loaders[i] == NULL)
			{
				I_VitaTrace("[Vita] Loader thread %d failed: %s\n", i + 1, SDL_GetError());
				// Threads already started stay parked on the semaphore; with
				// no run published they never touch a job.
				StartFailed = true;
				return false;
			}
		}
		Started = true;
		I_VitaTrace("[Vita] Loader threads started on cores 1 and 2\n");
		return true;
	}
}

void I_VitaRunLoadJobs(FVitaLoadJob **jobs, unsigned count, FVitaLoadPoolStats &stats)
{
	const double ms = 1000.0 / SDL_GetPerformanceFrequency();
	const Uint64 wallStart = SDL_GetPerformanceCounter();
	stats.jobs = count;
	stats.mainDecoded = 0;
	stats.readMs = stats.wallMs = stats.decodeMs = 0;
	if (count == 0) return;

	if (!StartLoaders())
	{
		Uint64 readTicks = 0, decodeTicks = 0;
		for (unsigned i = 0; i < count; ++i)
		{
			Uint64 t0 = SDL_GetPerformanceCounter();
			jobs[i]->Read();
			Uint64 t1 = SDL_GetPerformanceCounter();
			jobs[i]->Decode();
			readTicks += t1 - t0;
			decodeTicks += SDL_GetPerformanceCounter() - t1;
		}
		for (unsigned i = 0; i < count; ++i) jobs[i]->Finish();
		stats.mainDecoded = count;
		stats.readMs = readTicks * ms;
		stats.decodeMs = decodeTicks * ms;
		stats.wallMs = (SDL_GetPerformanceCounter() - wallStart) * ms;
		return;
	}

	FRun run;
	run.jobs = jobs;
	run.count = count;
	run.ready.store(0);
	run.next.store(0);
	run.done.store(0);
	run.inFlight.store(0);
	run.decodeTicks.store(0);
	run.sizes = new size_t[count];
	Current.store(&run, std::memory_order_release);

	Uint64 readTicks = 0;
	unsigned index;
	for (unsigned i = 0; i < count; ++i)
	{
		// Over the memory cap: decode on this thread until under it again.
		while (run.inFlight.load(std::memory_order_relaxed) > MAX_IN_FLIGHT)
		{
			if (Claim(run, index)) { DecodeOne(run, index); ++stats.mainDecoded; }
			else SDL_Delay(1);
		}
		const Uint64 t0 = SDL_GetPerformanceCounter();
		run.sizes[i] = jobs[i]->Read();
		readTicks += SDL_GetPerformanceCounter() - t0;
		run.inFlight.fetch_add(run.sizes[i], std::memory_order_relaxed);
		run.ready.store(i + 1, std::memory_order_release);
		SDL_SemPost(WorkSem);
	}
	// Everything is read: help decode, then wait for the loaders' last jobs.
	while (Claim(run, index)) { DecodeOne(run, index); ++stats.mainDecoded; }
	while (run.done.load(std::memory_order_acquire) < count) SDL_Delay(1);
	Current.store(NULL);
	while (Busy.load() > 0) SDL_Delay(1);

	for (unsigned i = 0; i < count; ++i) jobs[i]->Finish();
	delete[] run.sizes;
	stats.readMs = readTicks * ms;
	stats.decodeMs = run.decodeTicks.load() * ms;
	stats.wallMs = (SDL_GetPerformanceCounter() - wallStart) * ms;
}

#endif
