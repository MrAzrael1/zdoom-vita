

#ifndef __RESFILE_H
#define __RESFILE_H

#include "files.h"

class FResourceFile;
class FTexture;

struct FResourceLump
{
	friend class FResourceFile;

	int				LumpSize;
	FString			FullName;		// only valid for files loaded from a non-wad archive
	union
	{
		char		Name[9];

		DWORD		dwName;			// These are for accessing the first 4 or 8 chars of
		QWORD		qwName;			// Name as a unit without breaking strict aliasing rules
	};
	BYTE			Flags;
	SBYTE			RefCount;
	char *			Cache;
	FResourceFile *	Owner;
	FTexture *		LinkedTexture;
	int				Namespace;

	FResourceLump()
	{
		Cache = NULL;
		Owner = NULL;
		Flags = 0;
		RefCount = 0;
		Namespace = 0;	// ns_global
		*Name = 0;
		LinkedTexture = NULL;
	}

	virtual ~FResourceLump();
	virtual FileReader *GetReader();
	virtual FileReader *NewReader();
	virtual int GetFileOffset() { return -1; }
	virtual int GetIndexNum() const { return 0; }
#ifdef ZD_VITA
	// Where this lump's bytes sit in its container, so map loads can fetch
	// many lumps with a few large sequential reads (posix/vita_bulkread.cpp).
	// 'start' and 'readSize' are positions in 'reader'; with localHeader the
	// range begins at a zip local file header (with slack for its variable
	// part) that must be skipped. Returns false for lumps that can only be
	// read through FillCache (already cached, in memory, other compression).
	struct VitaRawInfo
	{
		FileReader *reader;
		long start, readSize, compressedSize;
		bool deflated, localHeader;
	};
	virtual bool VitaGetRawInfo(VitaRawInfo &info) { return false; }
#endif
	void LumpNameSetup(FString iname);
	void CheckEmbedded();

	void *CacheLump();
	int ReleaseCache();

protected:
	virtual int FillCache() = 0;

};

class FResourceFile
{
public:
	FileReader *Reader;
	const char *Filename;
protected:
	DWORD NumLumps;

	FResourceFile(const char *filename, FileReader *r);

	// for archives that can contain directories
	void PostProcessArchive(void *lumps, size_t lumpsize);

private:
	DWORD FirstLump;

	int FilterLumps(FString filtername, void *lumps, size_t lumpsize, DWORD max);
	int FilterLumpsByGameType(int gametype, void *lumps, size_t lumpsize, DWORD max);
	bool FindPrefixRange(FString filter, void *lumps, size_t lumpsize, DWORD max, DWORD &start, DWORD &end);
	void JunkLeftoverFilters(void *lumps, size_t lumpsize, DWORD max);

public:
	static FResourceFile *OpenResourceFile(const char *filename, FileReader *file, bool quiet = false);
	static FResourceFile *OpenDirectory(const char *filename, bool quiet = false);
	virtual ~FResourceFile();
	FileReader *GetReader() const { return Reader; }
	DWORD LumpCount() const { return NumLumps; }
	DWORD GetFirstLump() const { return FirstLump; }
	void SetFirstLump(DWORD f) { FirstLump = f; }

	virtual void FindStrifeTeaserVoices ();
	virtual bool Open(bool quiet) = 0;
	virtual FResourceLump *GetLump(int no) = 0;
};

struct FUncompressedLump : public FResourceLump
{
	int				Position;

	virtual FileReader *GetReader();
	virtual int FillCache();
	virtual int GetFileOffset() { return Position; }
#ifdef ZD_VITA
	virtual bool VitaGetRawInfo(VitaRawInfo &info);
#endif

};


// Base class for uncompressed resource files (WAD, GRP, PAK and single lumps)
class FUncompressedFile : public FResourceFile
{
protected:
	FUncompressedLump * Lumps;


	FUncompressedFile(const char *filename, FileReader *r);
	virtual ~FUncompressedFile();
	virtual FResourceLump *GetLump(int no) { return ((unsigned)no < NumLumps)? &Lumps[no] : NULL; }

public:
};


struct FExternalLump : public FResourceLump
{
	const char *filename;	// the actual file name. This is not necessarily the same as the lump name!

	FExternalLump(const char *_filename, int filesize = -1);
	~FExternalLump();
	virtual int FillCache();

};






#endif