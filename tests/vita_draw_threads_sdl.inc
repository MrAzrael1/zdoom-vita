// Minimal SDL thread/semaphore/timer functions for the draw-thread test,
// implemented with the C++ standard library.
#include <SDL.h>
#include <SDL_thread.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

struct SDL_semaphore { std::mutex m; std::condition_variable cv; unsigned count; };
struct SDL_Thread { std::thread t; };

extern "C" {
SDL_sem *SDL_CreateSemaphore(Uint32 initial) { SDL_sem *s = new SDL_sem; s->count = initial; return s; }
void SDL_DestroySemaphore(SDL_sem *s) { delete s; }
int SDL_SemWait(SDL_sem *s)
{
	std::unique_lock<std::mutex> lock(s->m);
	s->cv.wait(lock, [s] { return s->count > 0; });
	--s->count;
	return 0;
}
int SDL_SemPost(SDL_sem *s)
{
	{ std::lock_guard<std::mutex> lock(s->m); ++s->count; }
	s->cv.notify_one();
	return 0;
}
SDL_Thread *SDL_CreateThreadWithStackSize(SDL_ThreadFunction fn, const char *, const size_t, void *data)
{
	SDL_Thread *t = new SDL_Thread;
	t->t = std::thread([fn, data] { fn(data); });
	return t;
}
void SDL_WaitThread(SDL_Thread *t, int *) { t->t.join(); delete t; }
void SDL_Delay(Uint32 ms) { if (ms) std::this_thread::sleep_for(std::chrono::milliseconds(ms)); else std::this_thread::yield(); }
Uint64 SDL_GetPerformanceCounter(void)
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
Uint64 SDL_GetPerformanceFrequency(void) { return 1000000000ull; }
const char *SDL_GetError(void) { return "test"; }
}
