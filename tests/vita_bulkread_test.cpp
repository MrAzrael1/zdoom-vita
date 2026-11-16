// Checks FVitaBulkReader + I_VitaUnpackLump against a real zip: every lump's
// bytes must match Python's zipfile output, in sorted and unsorted orders,
// with local headers whose extra fields exceed the slack (fallback path).
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <random>
#include "w_wad.h"
#include "vita_bulkread.h"
FakeWads Wads;
int main(int argc, char **argv) {
	const char *zip = argv[1];
	FileReader reader(zip);
	std::ifstream idx(std::string(zip) + ".idx");
	std::vector<std::string> names;
	{ // names in order from the zip listing written by make_zip.py
		std::ifstream l(std::string(zip) + ".names"); std::string s; while (std::getline(l, s)) names.push_back(s);
	}
	long off, csize, usize; int defl, nlen; int i = 0;
	while (idx >> off >> csize >> usize >> defl >> nlen) {
		FResourceLump L; L.ok = true;
		L.info.reader = &reader; L.info.start = off; L.info.compressedSize = csize; L.info.deflated = defl; L.info.localHeader = true;
		L.info.readSize = csize + 30 + nlen + 512;
		Wads.lumps.push_back(L);
		std::ifstream c(std::string(zip) + "." + names[i], std::ios::binary);
		Wads.contents.push_back(std::vector<unsigned char>((std::istreambuf_iterator<char>(c)), std::istreambuf_iterator<char>()));
		++i;
	}
	int errors = 0;
	for (int order = 0; order < 3; ++order) {
		std::vector<int> lumps(Wads.lumps.size());
		for (size_t k = 0; k < lumps.size(); ++k) lumps[k] = (int)k;
		if (order == 1) { std::mt19937 g(5); std::shuffle(lumps.begin(), lumps.end(), g); }
		if (order == 2) { std::mt19937 g(9); std::shuffle(lumps.begin(), lumps.end(), g); I_VitaSortLumpsByPosition(lumps); }
		reader.reads = 0; Wads.normalReads = 0;
		FVitaBulkReader bulk(lumps);
		for (size_t k = 0; k < lumps.size(); ++k) {
			FVitaRawLump raw; bulk.Fetch((unsigned)k, raw);
			long len; unsigned char *data = I_VitaUnpackLump(raw, len);
			const auto &want = Wads.contents[lumps[k]];
			if (len != (long)want.size() || (len && (!data || memcmp(data, want.data(), len)))) { ++errors; printf("mismatch lump %d len %ld want %zu\n", lumps[k], len, want.size()); }
			delete[] data;
		}
		printf("order %d: %zu lumps, %u spans (%d file reads), %u fallbacks\n", order, lumps.size(), bulk.BulkReads(), reader.reads, bulk.Fallbacks());
	}
	// Corrupt deflate data must be rejected, not crash.
	{ unsigned char junk[64]; for (int k = 0; k < 64; ++k) junk[k] = (unsigned char)(k * 37);
	  FVitaRawLump raw; raw.data = new unsigned char[64]; memcpy(raw.data, junk, 64); raw.size = 64; raw.unpackedSize = 5000; raw.deflated = true;
	  long len; unsigned char *d = I_VitaUnpackLump(raw, len); if (d || len) { ++errors; printf("corrupt data accepted\n"); } }
	printf(errors ? "FAIL %d\n" : "PASS\n", errors);
	return errors != 0;
}
