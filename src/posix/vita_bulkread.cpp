// Bulk lump fetching for parallel map-load decoding (Vita); see vita_bulkread.h.
#ifdef ZD_VITA

#include <string.h>
#include <algorithm>
#include <zlib.h>
#include "w_wad.h"
#include "files.h"
#include "m_swap.h"
#include "resourcefiles/resourcefile.h"
#include "vita_bulkread.h"

namespace
{
	enum
	{
		MAX_SPAN = 2 * 1024 * 1024,	// one read
		MAX_GAP = 64 * 1024,		// unrelated bytes worth reading through
		ZIP_LOCAL_HEADER = 30,
	};

	bool RawInfo(int lump, FResourceLump::VitaRawInfo &info)
	{
		FResourceLump *l = Wads.VitaGetLump(lump);
		return l != NULL && l->VitaGetRawInfo(info);
	}
}

void I_VitaLumpPosition(int lump, const void *&container, long &position)
{
	FResourceLump::VitaRawInfo info;
	if (RawInfo(lump, info)) { container = info.reader; position = info.start; }
	else { container = NULL; position = 0; }
}

void I_VitaSortLumpsByPosition(std::vector<int> &lumps)
{
	struct Key { int lump; FileReader *reader; long start; };
	std::vector<Key> keys(lumps.size());
	for (size_t i = 0; i < lumps.size(); ++i)
	{
		FResourceLump::VitaRawInfo info;
		keys[i].lump = lumps[i];
		if (RawInfo(lumps[i], info)) { keys[i].reader = info.reader; keys[i].start = info.start; }
		else { keys[i].reader = NULL; keys[i].start = 0; }
	}
	std::stable_sort(keys.begin(), keys.end(), [](const Key &a, const Key &b)
	{
		if (a.reader != b.reader) return a.reader < b.reader;
		return a.start < b.start;
	});
	for (size_t i = 0; i < lumps.size(); ++i) lumps[i] = keys[i].lump;
}

FVitaBulkReader::FVitaBulkReader(const std::vector<int> &lumps)
: Buffer(NULL), BufferCapacity(0), BufferStart(0), BufferLength(0), LoadedGroup(-1), Spans(0), Fallback(0)
{
	Entries.resize(lumps.size());
	for (size_t i = 0; i < lumps.size(); ++i)
	{
		Entry &e = Entries[i];
		FResourceLump::VitaRawInfo info;
		e.lump = lumps[i];
		e.bulk = RawInfo(lumps[i], info) && info.readSize > 0 && info.readSize <= MAX_SPAN;
		e.group = 0;
		if (!e.bulk) continue;
		e.reader = info.reader;
		e.start = info.start;
		e.readSize = info.readSize;
		e.compressedSize = info.compressedSize;
		e.unpackedSize = Wads.LumpLength(lumps[i]);
		e.deflated = info.deflated;
		e.localHeader = info.localHeader;

		// Join the previous group when it is in the same file, close behind,
		// and the span stays within one read.
		if (!Groups.empty())
		{
			Group &g = Groups.back();
			const long end = std::max(g.end, e.start + e.readSize);
			if (g.reader == e.reader && e.start >= g.start && e.start - g.end <= MAX_GAP && end - g.start <= MAX_SPAN)
			{
				g.end = end;
				e.group = unsigned(Groups.size() - 1);
				continue;
			}
		}
		Group g = { e.reader, e.start, e.start + e.readSize };
		Groups.push_back(g);
		e.group = unsigned(Groups.size() - 1);
	}
}

FVitaBulkReader::~FVitaBulkReader()
{
	delete[] Buffer;
}

void FVitaBulkReader::FetchNormally(int lump, FVitaRawLump &out)
{
	++Fallback;
	out.size = out.unpackedSize = Wads.LumpLength(lump);
	out.deflated = false;
	out.data = NULL;
	if (out.size <= 0) { out.size = out.unpackedSize = 0; return; }
	out.data = new BYTE[out.size];
	Wads.ReadLump(lump, out.data);
}

void FVitaBulkReader::Fetch(unsigned index, FVitaRawLump &out)
{
	const Entry &e = Entries[index];
	if (!e.bulk) { FetchNormally(e.lump, out); return; }

	if (LoadedGroup != int(e.group))
	{
		const Group &g = Groups[e.group];
		const long want = g.end - g.start;
		if (want > BufferCapacity)
		{
			delete[] Buffer;
			Buffer = new BYTE[want];
			BufferCapacity = want;
		}
		g.reader->Seek(g.start, SEEK_SET);
		// Short at the end of the file: the local-header slack can run past it.
		BufferLength = g.reader->Read(Buffer, want);
		if (BufferLength < 0) BufferLength = 0;
		BufferStart = g.start;
		LoadedGroup = int(e.group);
		++Spans;
	}

	long offset = e.start - BufferStart;
	if (e.localHeader)
	{
		// Skip the zip local file header, whose name/extra lengths are only
		// known once it has been read.
		const BYTE *h = Buffer + offset;
		if (offset + ZIP_LOCAL_HEADER > BufferLength || h[0] != 'P' || h[1] != 'K' || h[2] != 3 || h[3] != 4)
		{
			FetchNormally(e.lump, out);
			return;
		}
		offset += ZIP_LOCAL_HEADER + (h[26] | (h[27] << 8)) + (h[28] | (h[29] << 8));
	}
	if (offset + e.compressedSize > BufferLength)
	{
		FetchNormally(e.lump, out);	// header longer than the slack allowed
		return;
	}
	out.size = e.compressedSize;
	out.unpackedSize = e.unpackedSize;
	out.deflated = e.deflated;
	out.data = new BYTE[out.size > 0 ? out.size : 1];
	memcpy(out.data, Buffer + offset, out.size);
}

BYTE *I_VitaUnpackLump(FVitaRawLump &raw, long &length)
{
	length = 0;
	if (raw.data == NULL) return NULL;
	if (!raw.deflated)
	{
		BYTE *data = raw.data;
		raw.data = NULL;
		length = raw.size;
		return data;
	}
	BYTE *out = new BYTE[raw.unpackedSize > 0 ? raw.unpackedSize : 1];
	z_stream z;
	memset(&z, 0, sizeof(z));
	z.next_in = raw.data;
	z.avail_in = uInt(raw.size);
	z.next_out = out;
	z.avail_out = uInt(raw.unpackedSize);
	bool ok = inflateInit2(&z, -MAX_WBITS) == Z_OK;
	if (ok)
	{
		const int err = inflate(&z, Z_FINISH);
		ok = (err == Z_STREAM_END || err == Z_OK || err == Z_BUF_ERROR) && z.total_out == uLong(raw.unpackedSize);
		inflateEnd(&z);
	}
	delete[] raw.data;
	raw.data = NULL;
	if (!ok) { delete[] out; return NULL; }
	length = raw.unpackedSize;
	return out;
}

#endif
