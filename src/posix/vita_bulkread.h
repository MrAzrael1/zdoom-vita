// Bulk lump fetching for parallel map-load decoding (Vita).
//
// Map-load reads were latency-bound: about 9,400 small seek+read pairs on the
// memory card at roughly 1 ms each, plus a separate read of each zip entry's
// local header. FVitaBulkReader takes the lumps of a decode run in file order,
// groups neighbours into spans of up to 2 MiB, reads each span with one large
// read, and hands every lump its still-compressed bytes. Inflating then
// happens on the loader threads (I_VitaUnpackLump) instead of the game thread.
#ifndef __VITA_BULKREAD_H__
#define __VITA_BULKREAD_H__
#ifdef ZD_VITA

#include <vector>
#include "doomtype.h"

class FileReader;

struct FVitaRawLump
{
	BYTE *data;			// new[]; owned by the caller
	long size;			// bytes in data
	long unpackedSize;	// final lump size
	bool deflated;		// data is raw deflate and must be inflated
};

// Orders lumps by container and position, for requesting them in file order.
void I_VitaSortLumpsByPosition(std::vector<int> &lumps);
// Sort key for the same order (lumps that cannot be bulk-read sort first).
void I_VitaLumpPosition(int lump, const void *&container, long &position);

class FVitaBulkReader
{
public:
	// 'lumps' in the order they will be fetched (ideally sorted by position).
	explicit FVitaBulkReader(const std::vector<int> &lumps);
	~FVitaBulkReader();

	// Game thread. Fetches the lump at position 'index' of the list given to
	// the constructor. Lumps that cannot be bulk-read (or whose zip header is
	// unusual) are read normally and returned already unpacked.
	void Fetch(unsigned index, FVitaRawLump &out);

	unsigned BulkReads() const { return Spans; }
	unsigned Fallbacks() const { return Fallback; }

private:
	struct Entry { int lump; bool bulk; FileReader *reader; long start, readSize, compressedSize, unpackedSize; bool deflated, localHeader; unsigned group; };
	struct Group { FileReader *reader; long start, end; };
	std::vector<Entry> Entries;
	std::vector<Group> Groups;
	BYTE *Buffer;
	long BufferCapacity, BufferStart, BufferLength;
	int LoadedGroup;
	unsigned Spans, Fallback;

	void FetchNormally(int lump, FVitaRawLump &out);
};

// Any thread. Returns the final lump bytes (new[]), inflating if needed, and
// frees raw.data. NULL (and length 0) if the data is corrupt.
BYTE *I_VitaUnpackLump(FVitaRawLump &raw, long &length);

#endif
#endif
