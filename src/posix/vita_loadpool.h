// Parallel asset decoding for map loads (Vita).
//
// The game thread reads each job's input (lump data from the memory card and
// pk3 unpacking stay on one thread: FileReader is not thread-safe), and two
// loader threads on cores 1 and 2 decode in parallel. The game thread joins
// in decoding once it has read everything. Draw workers are idle during a
// load, so these cores are otherwise unused.
#ifndef __VITA_LOADPOOL_H__
#define __VITA_LOADPOOL_H__
#ifdef ZD_VITA

#include <stddef.h>

class FVitaLoadJob
{
public:
	virtual ~FVitaLoadJob() {}
	// Game thread, in job order: read the input. Returns its size in bytes
	// (used to bound memory held between reading and decoding).
	virtual size_t Read() = 0;
	// Any thread: decode from the input read above and free that input.
	// Must not touch engine state other than the job's own object.
	virtual void Decode() = 0;
	// Game thread, in job order, after every job has decoded.
	virtual void Finish() {}
};

struct FVitaLoadPoolStats
{
	unsigned jobs, mainDecoded;
	double readMs, wallMs, decodeMs;	// decodeMs: summed over all threads
};

// Runs Read on the game thread and Decode on all three cores, then Finish in
// order. Falls back to doing everything on the game thread if the loader
// threads cannot be started.
void I_VitaRunLoadJobs(FVitaLoadJob **jobs, unsigned count, FVitaLoadPoolStats &stats);

#endif
#endif
