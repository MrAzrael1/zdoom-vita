// Stress test for vita_loadpool: many jobs, random sizes, checks every job is
// read once in order, decoded exactly once, finished once in order, and that
// decode sees the data read for that job.
#include <SDL.h>
#include <vector>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "vita_loadpool.h"
static std::atomic<int> readOrder{0}, finishOrder{0}, errors{0};
struct TestJob : FVitaLoadJob {
	int id; size_t size; unsigned char *buf = nullptr; std::atomic<int> decoded{0}; unsigned sum = 0, expect = 0;
	size_t Read() override {
		if (readOrder.fetch_add(1) != id) errors++;
		buf = new unsigned char[size];
		for (size_t i = 0; i < size; ++i) { buf[i] = (unsigned char)(id * 31 + i); expect += buf[i]; }
		return size;
	}
	void Decode() override {
		if (decoded.fetch_add(1) != 0) errors++;
		for (size_t i = 0; i < size; ++i) sum += buf[i];
		volatile int spin = rand() % 2000; while (spin-- > 0) {}
		delete[] buf; buf = nullptr;
	}
	void Finish() override {
		if (finishOrder.fetch_add(1) != id) errors++;
		if (sum != expect || decoded.load() != 1) errors++;
	}
};
int main() {
	SDL_Init(0);
	for (int round = 0; round < 20; ++round) {
		unsigned n = 1 + rand() % 3000;
		std::vector<TestJob*> jobs; std::vector<FVitaLoadJob*> ptrs;
		readOrder = finishOrder = 0;
		for (unsigned i = 0; i < n; ++i) { auto *j = new TestJob; j->id = i; j->size = 1 + (rand() % 4 == 0 ? rand() % (4<<20) : rand() % 4096); jobs.push_back(j); ptrs.push_back(j); }
		FVitaLoadPoolStats st;
		I_VitaRunLoadJobs(ptrs.data(), n, st);
		if (readOrder != (int)n || finishOrder != (int)n) errors++;
		printf("round %d jobs=%u main=%u read=%.1f wall=%.1f decode=%.1f\n", round, st.jobs, st.mainDecoded, st.readMs, st.wallMs, st.decodeMs);
		for (auto *j : jobs) delete j;
	}
	{ FVitaLoadPoolStats z; I_VitaRunLoadJobs(nullptr, 0, z); }
	printf(errors ? "FAIL %d\n" : "PASS\n", errors.load());
	return errors != 0;
}
