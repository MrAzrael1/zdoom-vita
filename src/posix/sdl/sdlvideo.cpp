#include "posix/vita_debug.h"
#ifdef ZD_VITA
#include "posix/vita_launcher.h"
#endif
#include "posix/vita_perf.h"

// HEADER FILES ------------------------------------------------------------

#include "doomtype.h"

#include "templates.h"
#include "i_system.h"
#include "i_video.h"
#include "v_video.h"
#include "v_pfx.h"
#include "stats.h"
#include "v_palette.h"
#include "sdlvideo.h"
#include "r_swrenderer.h"
#include "version.h"

#include <SDL.h>

#ifdef __APPLE__
#include <OpenGL/OpenGL.h>
#endif // __APPLE__

// MACROS ------------------------------------------------------------------

// TYPES -------------------------------------------------------------------

class SDLFB : public DFrameBuffer
{
	DECLARE_CLASS(SDLFB, DFrameBuffer)
public:
	SDLFB (int width, int height, bool fullscreen, SDL_Window *oldwin);
	~SDLFB ();

	bool Lock (bool buffer);
	void Unlock ();
	bool Relock ();
	void ForceBuffering (bool force);
	bool IsValid ();
	void Update ();
	PalEntry *GetPalette ();
	void GetFlashedPalette (PalEntry pal[256]);
	void UpdatePalette ();
	bool SetGamma (float gamma);
	bool SetFlash (PalEntry rgb, int amount);
	void GetFlash (PalEntry &rgb, int &amount);
	void SetFullscreen (bool fullscreen);
	int GetPageCount ();
	bool IsFullscreen ();

	friend class SDLVideo;

	virtual void SetVSync (bool vsync);
	virtual void ScaleCoordsFromWindow(SWORD &x, SWORD &y);

private:
	PalEntry SourcePalette[256];
	BYTE GammaTable[3][256];
	PalEntry Flash;
	int FlashAmount;
	float Gamma;
	bool UpdatePending;

	SDL_Window *Screen;
	SDL_Renderer *Renderer;
	union
	{
		SDL_Texture *Texture;
		SDL_Surface *Surface;
	};

#ifdef ZD_VITA
	SDL_Rect VitaDestination;
#endif
	bool UsingRenderer;
	bool NeedPalUpdate;
	bool NeedGammaUpdate;
	bool NotPaletted;

	void UpdateColors ();
	void ResetSDLRenderer ();

	SDLFB () {}
};
IMPLEMENT_CLASS(SDLFB)

struct MiniModeInfo
{
	WORD Width, Height;
};

#ifdef ZD_VITA
#include "r_draw_mt.h"
void I_VitaTraceMemory(const char *when);
#endif

// PUBLIC FUNCTION PROTOTYPES ----------------------------------------------

// PRIVATE FUNCTION PROTOTYPES ---------------------------------------------

// EXTERNAL DATA DECLARATIONS ----------------------------------------------

extern IVideo *Video;
extern bool GUICapture;

EXTERN_CVAR (Float, Gamma)
EXTERN_CVAR (Int, vid_maxfps)
EXTERN_CVAR (Bool, cl_capfps)
EXTERN_CVAR (Bool, vid_vsync)

// PUBLIC DATA DEFINITIONS -------------------------------------------------

CVAR (Int, vid_adapter, 0, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)

CVAR (Int, vid_displaybits, 32, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)

CVAR (Bool, vid_forcesurface, false, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)

CUSTOM_CVAR (Float, rgamma, 1.f, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
{
	if (screen != NULL)
	{
		screen->SetGamma (Gamma);
	}
}
CUSTOM_CVAR (Float, ggamma, 1.f, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
{
	if (screen != NULL)
	{
		screen->SetGamma (Gamma);
	}
}
CUSTOM_CVAR (Float, bgamma, 1.f, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
{
	if (screen != NULL)
	{
		screen->SetGamma (Gamma);
	}
}

// PRIVATE DATA DEFINITIONS ------------------------------------------------

// Dummy screen sizes to pass when windowed
static MiniModeInfo WinModes[] =
{
	{ 320, 200 },
	{ 320, 240 },
	{ 400, 225 },	// 16:9
	{ 400, 300 },
	#ifdef ZD_VITA
	{ 480, 272 }, // Exact half-size Vita framebuffer
	#endif
	{ 480, 270 },	// 16:9
	{ 480, 360 },
	{ 512, 288 },	// 16:9
	{ 512, 384 },
#ifdef ZD_VITA
	{ 600, 340 }, // 1.6x output scaling
#endif
	{ 640, 360 },	// 16:9
	{ 640, 400 },
	{ 640, 480 },
#ifdef ZD_VITA
	{ 720, 408 }, // 3/4-size Vita framebuffer, 4/3 output scale
#endif
	{ 720, 480 },	// 16:10
	{ 720, 540 },
	{ 800, 450 },	// 16:9
	{ 800, 480 },
	{ 800, 500 },	// 16:10
	{ 800, 600 },
	{ 848, 480 },	// 16:9
#ifdef ZD_VITA
	{ 960, 544 },
#endif
	{ 960, 600 },	// 16:10
	{ 960, 720 },
	{ 1024, 576 },	// 16:9
	{ 1024, 600 },	// 17:10
	{ 1024, 640 },	// 16:10
	{ 1024, 768 },
	{ 1088, 612 },	// 16:9
	{ 1152, 648 },	// 16:9
	{ 1152, 720 },	// 16:10
	{ 1152, 864 },
	{ 1280, 720 },	// 16:9
	{ 1280, 854 },
	{ 1280, 800 },	// 16:10
	{ 1280, 960 },
	{ 1280, 1024 },	// 5:4
	{ 1360, 768 },	// 16:9
	{ 1366, 768 },
	{ 1400, 787 },	// 16:9
	{ 1400, 875 },	// 16:10
	{ 1400, 1050 },
	{ 1440, 900 },
	{ 1440, 960 },
	{ 1440, 1080 },
	{ 1600, 900 },	// 16:9
	{ 1600, 1000 },	// 16:10
	{ 1600, 1200 },
	{ 1680, 1050 },	// 16:10
	{ 1920, 1080 },
	{ 1920, 1200 },
	{ 2048, 1536 },
	{ 2560, 1440 },
	{ 2560, 1600 },
	{ 2560, 2048 },
	{ 2880, 1800 },
	{ 3200, 1800 },
	{ 3840, 2160 },
	{ 3840, 2400 },
	{ 4096, 2160 },
	{ 5120, 2880 }
};

static cycle_t BlitCycles;
static cycle_t SDLFlipCycles;

// CODE --------------------------------------------------------------------

void ScaleWithAspect (int &w, int &h, int Width, int Height)
{
	int resRatio = CheckRatio (Width, Height);
	int screenRatio;
	CheckRatio (w, h, &screenRatio);
	if (resRatio == screenRatio)
		return;

	double yratio;
	switch(resRatio)
	{
		case 0: yratio = 4./3.; break;
		case 1: yratio = 16./9.; break;
		case 2: yratio = 16./10.; break;
		case 3: yratio = 17./10.; break;
		case 4: yratio = 5./4.; break;
		default: return;
	}
	double y = w/yratio;
	if (y > h)
		w = h*yratio;
	else
		h = y;
}

SDLVideo::SDLVideo (int parm)
{
	IteratorBits = 0;
}

SDLVideo::~SDLVideo ()
{
}

void SDLVideo::StartModeIterator (int bits, bool fs)
{
	IteratorMode = 0;
	IteratorBits = bits;
}

bool SDLVideo::NextMode (int *width, int *height, bool *letterbox)
{
	if (IteratorBits != 8)
		return false;

	if ((unsigned)IteratorMode < sizeof(WinModes)/sizeof(WinModes[0]))
	{
		*width = WinModes[IteratorMode].Width;
		*height = WinModes[IteratorMode].Height;
		++IteratorMode;
		return true;
	}
	return false;
}

DFrameBuffer *SDLVideo::CreateFrameBuffer (int width, int height, bool fullscreen, DFrameBuffer *old)
{
	static int retry = 0;
	static int owidth, oheight;
	
	PalEntry flashColor;
	int flashAmount;

	SDL_Window *oldwin = NULL;

	if (old != NULL)
	{ // Reuse the old framebuffer if its attributes are the same
		SDLFB *fb = static_cast<SDLFB *> (old);
		if (fb->Width == width &&
			fb->Height == height)
		{
			bool fsnow = (SDL_GetWindowFlags (fb->Screen) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
	
			if (fsnow != fullscreen)
			{
				fb->SetFullscreen (fullscreen);
			}
			return old;
		}

		oldwin = fb->Screen;
		fb->Screen = NULL;

		old->GetFlash (flashColor, flashAmount);
		old->ObjectFlags |= OF_YesReallyDelete;
		if (screen == old) screen = NULL;
		delete old;
	}
	else
	{
		flashColor = 0;
		flashAmount = 0;
	}
	
	SDLFB *fb = new SDLFB (width, height, fullscreen, oldwin);
	
	// If we could not create the framebuffer, try again with slightly
	// different parameters in this order:
	// 1. Try with the closest size
	// 2. Try in the opposite screen mode with the original size
	// 3. Try in the opposite screen mode with the closest size
	// This is a somewhat confusing mass of recursion here.

	while (fb == NULL || !fb->IsValid ())
	{
		if (fb != NULL)
		{
			delete fb;
		}

		switch (retry)
		{
		case 0:
			owidth = width;
			oheight = height;
		case 2:
			// Try a different resolution. Hopefully that will work.
			I_ClosestResolution (&width, &height, 8);
			break;

		case 1:
			// Try changing fullscreen mode. Maybe that will work.
			width = owidth;
			height = oheight;
			fullscreen = !fullscreen;
			break;

		default:
			// I give up!
			I_FatalError ("Could not create new screen (%d x %d)", owidth, oheight);
		}

		++retry;
		fb = static_cast<SDLFB *>(CreateFrameBuffer (width, height, fullscreen, NULL));
	}
	retry = 0;

	fb->SetFlash (flashColor, flashAmount);

	return fb;
}

void SDLVideo::SetWindowedScale (float scale)
{
}

// FrameBuffer implementation -----------------------------------------------

SDLFB::SDLFB (int width, int height, bool fullscreen, SDL_Window *oldwin)
	: DFrameBuffer (width, height)
{
#ifdef ZD_VITA
	I_VitaStartupEnd();
#endif

	int i;
	
	NeedPalUpdate = false;
	NeedGammaUpdate = false;
	UpdatePending = false;
	NotPaletted = false;
	FlashAmount = 0;

	if (oldwin)
	{
		// In some cases (Mac OS X fullscreen) SDL2 doesn't like having multiple windows which
		// appears to inevitably happen while compositor animations are running. So lets try
		// to reuse the existing window.
		Screen = oldwin;
#ifdef ZD_VITA
		SDL_SetWindowSize (Screen, 960, 544);
#else
		SDL_SetWindowSize (Screen, width, height);
#endif
		SetFullscreen (fullscreen);
	}
	else
	{
		FString caption;
		caption.Format(GAMESIG " %s (%s)", GetVersionString(), GetGitTime());

#ifdef ZD_VITA
		I_VitaTrace("[Vita] Display: creating window %dx%d\n", width, height);
#endif
		Screen = SDL_CreateWindow (caption,
			SDL_WINDOWPOS_UNDEFINED_DISPLAY(vid_adapter), SDL_WINDOWPOS_UNDEFINED_DISPLAY(vid_adapter),
#ifdef ZD_VITA
			960, 544, SDL_WINDOW_FULLSCREEN);
#else
			width, height, (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0)|SDL_WINDOW_RESIZABLE);
#endif

		if (Screen == NULL)
			return;
	}

	Renderer = NULL;
	Texture = NULL;
	ResetSDLRenderer ();

	for (i = 0; i < 256; i++)
	{
		GammaTable[0][i] = GammaTable[1][i] = GammaTable[2][i] = i;
	}

	memcpy (SourcePalette, GPalette.BaseColors, sizeof(PalEntry)*256);
	UpdateColors ();

#ifdef __APPLE__
	SetVSync (vid_vsync);
#endif
}


SDLFB::~SDLFB ()
{
	if (Renderer)
	{
		if (Texture)
			SDL_DestroyTexture (Texture);
		SDL_DestroyRenderer (Renderer);
	}

	if(Screen)
	{
		SDL_DestroyWindow (Screen);
	}
}

bool SDLFB::IsValid ()
{
	return DFrameBuffer::IsValid() && Screen != NULL;
}

int SDLFB::GetPageCount ()
{
	return 1;
}

bool SDLFB::Lock (bool buffered)
{
	return DSimpleCanvas::Lock ();
}

bool SDLFB::Relock ()
{
	return DSimpleCanvas::Lock ();
}

void SDLFB::Unlock ()
{
	if (UpdatePending && LockCount == 1)
	{
		Update ();
	}
	else if (--LockCount <= 0)
	{
		Buffer = NULL;
		LockCount = 0;
	}
}

void SDLFB::Update ()
{
	if (LockCount != 1)
	{
		if (LockCount > 0)
		{
			UpdatePending = true;
			--LockCount;
		}
		return;
	}

	DrawRateStuff ();

#ifdef ZD_VITA
	if (!cl_capfps)
	{
		I_WaitForFPSLimit();	// vita_framecap; does nothing when unlimited
	}
#elif !defined(__APPLE__)
	if(vid_maxfps && !cl_capfps)
	{
		SEMAPHORE_WAIT(FPSLimitSemaphore)
	}
#endif

	Buffer = NULL;
	LockCount = 0;
	UpdatePending = false;

	BlitCycles.Reset();
	SDLFlipCycles.Reset();
	BlitCycles.Clock();

#ifdef ZD_VITA
	Uint64 vitaConvertStart = I_VitaPerfClock();
#endif
	void *pixels;
	int pitch;
	if (UsingRenderer)
	{
		if (SDL_LockTexture (Texture, NULL, &pixels, &pitch))
			return;
	}
	else
	{
		if (SDL_LockSurface (Surface))
			return;

		pixels = Surface->pixels;
		pitch = Surface->pitch;
	}

	if (NotPaletted)
	{
		// Single-threaded on purpose: splitting this across the draw workers
		// was measured slower on Vita (multicore pass04). The destination is
		// the locked GPU texture, so the copy is memory-bound, not CPU-bound.
		GPfx.Convert (MemBuffer, Pitch,
			pixels, pitch, Width, Height,
			FRACUNIT, FRACUNIT, 0, 0);
	}
	else
	{
		if (pitch == Pitch)
		{
			memcpy (pixels, MemBuffer, Width*Height);
		}
		else
		{
			for (int y = 0; y < Height; ++y)
			{
				memcpy ((BYTE *)pixels+y*pitch, MemBuffer+y*Pitch, Width);
			}
		}
	}

	if (UsingRenderer)
	{
		SDL_UnlockTexture (Texture);

		SDLFlipCycles.Clock();
#ifdef ZD_VITA
		I_VitaPerfAdd(VPERF_CONVERT, vitaConvertStart);
		Uint64 vitaPresentStart = I_VitaPerfClock();
		// Opaque full-screen copies cover every pixel in exact-ratio modes.
		if (VitaDestination.x != 0 || VitaDestination.y != 0) SDL_RenderClear(Renderer);
#else
		SDL_RenderClear(Renderer);
#endif
#ifdef ZD_VITA
		SDL_RenderCopy(Renderer, Texture, NULL, &VitaDestination);
#else
		SDL_RenderCopy(Renderer, Texture, NULL, NULL);
#endif
		SDL_RenderPresent(Renderer);
#ifdef ZD_VITA
		I_VitaPerfAdd(VPERF_PRESENT, vitaPresentStart);
#endif
		SDLFlipCycles.Unclock();
	}
	else
	{
		SDL_UnlockSurface (Surface);

		SDLFlipCycles.Clock();
		SDL_UpdateWindowSurface (Screen);
		SDLFlipCycles.Unclock();
	}

	BlitCycles.Unclock();

	if (NeedGammaUpdate)
	{
		bool Windowed = false;
		NeedGammaUpdate = false;
		CalcGamma ((Windowed || rgamma == 0.f) ? Gamma : (Gamma * rgamma), GammaTable[0]);
		CalcGamma ((Windowed || ggamma == 0.f) ? Gamma : (Gamma * ggamma), GammaTable[1]);
		CalcGamma ((Windowed || bgamma == 0.f) ? Gamma : (Gamma * bgamma), GammaTable[2]);
		NeedPalUpdate = true;
	}
	
	if (NeedPalUpdate)
	{
		NeedPalUpdate = false;
		UpdateColors ();
	}
}

void SDLFB::UpdateColors ()
{
	if (NotPaletted)
	{
		PalEntry palette[256];
		
		for (int i = 0; i < 256; ++i)
		{
			palette[i].r = GammaTable[0][SourcePalette[i].r];
			palette[i].g = GammaTable[1][SourcePalette[i].g];
			palette[i].b = GammaTable[2][SourcePalette[i].b];
		}
		if (FlashAmount)
		{
			DoBlending (palette, palette,
				256, GammaTable[0][Flash.r], GammaTable[1][Flash.g], GammaTable[2][Flash.b],
				FlashAmount);
		}
		GPfx.SetPalette (palette);
	}
	else
	{
		SDL_Color colors[256];
		
		for (int i = 0; i < 256; ++i)
		{
			colors[i].r = GammaTable[0][SourcePalette[i].r];
			colors[i].g = GammaTable[1][SourcePalette[i].g];
			colors[i].b = GammaTable[2][SourcePalette[i].b];
		}
		if (FlashAmount)
		{
			DoBlending ((PalEntry *)colors, (PalEntry *)colors,
				256, GammaTable[2][Flash.b], GammaTable[1][Flash.g], GammaTable[0][Flash.r],
				FlashAmount);
		}
		SDL_SetPaletteColors (Surface->format->palette, colors, 0, 256);
	}
}

PalEntry *SDLFB::GetPalette ()
{
	return SourcePalette;
}

void SDLFB::UpdatePalette ()
{
	NeedPalUpdate = true;
}

bool SDLFB::SetGamma (float gamma)
{
	Gamma = gamma;
	NeedGammaUpdate = true;
	return true;
}

bool SDLFB::SetFlash (PalEntry rgb, int amount)
{
	Flash = rgb;
	FlashAmount = amount;
	NeedPalUpdate = true;
	return true;
}

void SDLFB::GetFlash (PalEntry &rgb, int &amount)
{
	rgb = Flash;
	amount = FlashAmount;
}

// Q: Should I gamma adjust the returned palette?
void SDLFB::GetFlashedPalette (PalEntry pal[256])
{
	memcpy (pal, SourcePalette, 256*sizeof(PalEntry));
	if (FlashAmount)
	{
		DoBlending (pal, pal, 256, Flash.r, Flash.g, Flash.b, FlashAmount);
	}
}

void SDLFB::SetFullscreen (bool fullscreen)
{
#ifdef ZD_VITA
	fullscreen = true;
#endif
	if (IsFullscreen() == fullscreen)
		return;

	SDL_SetWindowFullscreen (Screen, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
	if (!fullscreen)
	{
		// Restore proper window size
		#ifdef ZD_VITA
		SDL_SetWindowSize (Screen, 960, 544);
#else
		SDL_SetWindowSize (Screen, Width, Height);
#endif
	}

	ResetSDLRenderer ();
}

bool SDLFB::IsFullscreen ()
{
	return (SDL_GetWindowFlags (Screen) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
}

void SDLFB::ResetSDLRenderer ()
{
	if (Renderer)
	{
		if (Texture)
			SDL_DestroyTexture (Texture);
		SDL_DestroyRenderer (Renderer);
	}

	UsingRenderer = !vid_forcesurface;
	if (UsingRenderer)
	{
#ifdef ZD_VITA
		I_VitaTrace("[Vita] Display: creating SDL renderer\n");
#endif
		Renderer = SDL_CreateRenderer (Screen, -1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_TARGETTEXTURE|
										(vid_vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
		if (!Renderer)
			return;

		SDL_SetRenderDrawColor(Renderer, 0, 0, 0, 255);

#ifdef ZD_VITA
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); // Nearest scaling of the internal framebuffer
#endif
		Uint32 fmt;
		switch(vid_displaybits)
		{
			default: fmt = SDL_PIXELFORMAT_ARGB8888; break;
			case 30: fmt = SDL_PIXELFORMAT_ARGB2101010; break;
			case 24: fmt = SDL_PIXELFORMAT_RGB888; break;
			case 16: fmt = SDL_PIXELFORMAT_RGB565; break;
			case 15: fmt = SDL_PIXELFORMAT_ARGB1555; break;
		}
		Texture = SDL_CreateTexture (Renderer, fmt, SDL_TEXTUREACCESS_STREAMING, Width, Height);
#ifdef ZD_VITA
		I_VitaTrace("[Vita] Display: streaming texture %s (%s)\n", Texture != NULL ? "created" : "failed", SDL_GetError());
		if (Texture == NULL) return;
		SDL_SetTextureBlendMode(Texture, SDL_BLENDMODE_NONE);
#endif

		{
			NotPaletted = true;

			Uint32 format;
			SDL_QueryTexture(Texture, &format, NULL, NULL, NULL);

			Uint32 Rmask, Gmask, Bmask, Amask;
			int bpp;
			SDL_PixelFormatEnumToMasks(format, &bpp, &Rmask, &Gmask, &Bmask, &Amask);
			GPfx.SetFormat (bpp, Rmask, Gmask, Bmask);
		}
	}
	else
	{
		Surface = SDL_GetWindowSurface (Screen);

		if (Surface->format->palette == NULL)
		{
			NotPaletted = true;
			GPfx.SetFormat (Surface->format->BitsPerPixel, Surface->format->Rmask, Surface->format->Gmask, Surface->format->Bmask);
		}
		else
			NotPaletted = false;
	}

#ifdef ZD_VITA
	if (Renderer != NULL)
	{
		// Use physical output coordinates. The Vita backend did not correctly
		// apply SDL logical-size scaling to the default RenderCopy rectangle.
		SDL_RenderSetLogicalSize(Renderer, 0, 0);
		SDL_RenderSetScale(Renderer, 1.0f, 1.0f);
		int windowWidth=0, windowHeight=0;
		SDL_GetWindowSize(Screen, &windowWidth, &windowHeight);
		int outputWidth=960, outputHeight=544;
		if (SDL_GetRendererOutputSize(Renderer, &outputWidth, &outputHeight) != 0
			|| outputWidth <= 0 || outputHeight <= 0)
		{
			outputWidth=960; outputHeight=544;
		}
		I_VitaTrace("[Vita] Display: window=%dx%d flags=%08x\n", windowWidth, windowHeight, (unsigned)SDL_GetWindowFlags(Screen));
		SDL_Rect viewport = { 0, 0, outputWidth, outputHeight };
		SDL_RenderSetViewport(Renderer, &viewport);
		SDL_RenderSetClipRect(Renderer, NULL);
		VitaDestination = viewport;
		if (Width * outputHeight > Height * outputWidth)
			VitaDestination.h = outputWidth * Height / Width;
		else
			VitaDestination.w = outputHeight * Width / Height;
		VitaDestination.x = (outputWidth - VitaDestination.w) / 2;
		VitaDestination.y = (outputHeight - VitaDestination.h) / 2;
		I_VitaTrace("[Vita] Display: render=%dx%d output=%dx%d destination=%d,%d %dx%d\n",
			Width, Height, outputWidth, outputHeight, VitaDestination.x, VitaDestination.y,
			VitaDestination.w, VitaDestination.h);
	}
#else
	// In fullscreen, set logical size according to animorphic ratio.
	// Windowed modes are rendered to fill the window (usually 1:1)
	if (IsFullscreen ())
	{
		int w, h;
		SDL_GetWindowSize (Screen, &w, &h);
		ScaleWithAspect (w, h, Width, Height);
		SDL_RenderSetLogicalSize (Renderer, w, h);
	}
#endif
}

void SDLFB::SetVSync (bool vsync)
{
#ifdef __APPLE__
	if (CGLContextObj context = CGLGetCurrentContext())
	{
		// Apply vsync for native backend only (where OpenGL context is set)

#if MAC_OS_X_VERSION_MAX_ALLOWED < 1050
		// Inconsistency between 10.4 and 10.5 SDKs:
		// third argument of CGLSetParameter() is const long* on 10.4 and const GLint* on 10.5
		// So, GLint typedef'ed to long instead of int to workaround this issue
		typedef long GLint;
#endif // prior to 10.5

		const GLint value = vsync ? 1 : 0;
		CGLSetParameter(context, kCGLCPSwapInterval, &value);
	}
#elif defined(ZD_VITA)
	// The Vita GXM renderer switches vsync in place (its display queue waits
	// for the vertical refresh after each flip); recreating the renderer would
	// reallocate all of its video memory.
	if (Renderer != NULL)
	{
		const int result = SDL_RenderSetVSync (Renderer, vsync ? 1 : 0);
		I_VitaTrace ("[Vita] Display: vsync %s (result %d)\n", vsync ? "on" : "off", result);
	}
#else
	ResetSDLRenderer ();
#endif // __APPLE__
}

void SDLFB::ScaleCoordsFromWindow(SWORD &x, SWORD &y)
{
	int w, h;
	SDL_GetWindowSize (Screen, &w, &h);

	// Detect if we're doing scaling in the Window and adjust the mouse
	// coordinates accordingly. This could be more efficent, but I
	// don't think performance is an issue in the menus.
	if(IsFullscreen())
	{
		int realw = w, realh = h;
		ScaleWithAspect (realw, realh, SCREENWIDTH, SCREENHEIGHT);
		if (realw != SCREENWIDTH || realh != SCREENHEIGHT)
		{
			double xratio = (double)SCREENWIDTH/realw;
			double yratio = (double)SCREENHEIGHT/realh;
			if (realw < w)
			{
				x = (x - (w - realw)/2)*xratio;
				y *= yratio;
			}
			else
			{
				y = (y - (h - realh)/2)*yratio;
				x *= xratio;
			}
		}
	}
	else
	{
		x = (SWORD)(x*Width/w);
		y = (SWORD)(y*Height/h);
	}
}

ADD_STAT (blit)
{
	FString out;
	out.Format ("blit=%04.1f ms  flip=%04.1f ms",
		BlitCycles.TimeMS(), SDLFlipCycles.TimeMS());
	return out;
}

#ifdef ZD_VITA
CVAR(Bool, vita_profile, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
bool I_VitaPerfEnabled() { return vita_profile; }
// Nested timings overlap: thinkers are part of game, sprites are part of render.
// Keep the slowest frame's breakdown as well as averages; explosions are brief.
static Uint64 VitaPerfTotals[VPERF_COUNT], VitaPerfCurrent[VPERF_COUNT];
static Uint64 VitaPerfWorst[VPERF_COUNT], VitaPerfWindowStart, VitaPerfMaxFrame;
static unsigned VitaPerfFrames, VitaPerfSlowFrames, VitaPerfSpritesCurrent;
static unsigned VitaPerfSpritesTotal, VitaPerfSpritesPeak, VitaPerfSpritesWorst;
bool VitaLoadActive;
static Uint64 VitaLoadTotals[VLOAD_COUNT], VitaLoadStart, VitaLoadLastMark;
static unsigned VitaLoadReads, VitaLoadZipLumps;
static SDL_threadID VitaGameThread;
void I_VitaLoadBegin()
{
	VitaGameThread = SDL_ThreadID();
	memset(VitaLoadTotals, 0, sizeof(VitaLoadTotals));
	VitaLoadReads = VitaLoadZipLumps = 0;
	VitaLoadStart = VitaLoadLastMark = SDL_GetPerformanceCounter();
	VitaLoadActive = true;
}
void I_VitaLoadMark(EVitaLoadStage stage)
{
	if (!VitaLoadActive) return;
	Uint64 now = SDL_GetPerformanceCounter();
	VitaLoadTotals[stage] += now - VitaLoadLastMark;
	VitaLoadLastMark = now;
}
void I_VitaLoadAdd(EVitaLoadStage stage, Uint64 start)
{
	if (!VitaLoadActive || !start) return;
	VitaLoadTotals[stage] += SDL_GetPerformanceCounter() - start;
	if (stage == VLOAD_FILE_READ) ++VitaLoadReads;
	else if (stage == VLOAD_ZIP) ++VitaLoadZipLumps;
}
void I_VitaLoadEnd(const char *mapname)
{
	if (!VitaLoadActive) return;
	I_VitaLoadMark(VLOAD_FINISH);
	VitaLoadActive = false;
	const double ms = 1000.0 / SDL_GetPerformanceFrequency();
	const Uint64 *t = VitaLoadTotals;
	I_VitaTrace("[Vita] Load %s: total=%.0f ms; prep=%.0f music=%.0f mapdata=%.0f scripts=%.0f nodes=%.0f blockmap=%.0f things=%.0f specials=%.0f "
		"textures=%.0f effect-sprites=%.0f sounds=%.0f effect-sounds=%.0f finish=%.0f ms\n",
		mapname, (VitaLoadLastMark - VitaLoadStart) * ms,
		t[VLOAD_PREP]*ms, t[VLOAD_MUSIC]*ms, t[VLOAD_MAPDATA]*ms, t[VLOAD_SCRIPTS]*ms, t[VLOAD_NODES]*ms, t[VLOAD_BLOCKMAP]*ms, t[VLOAD_THINGS]*ms,
		t[VLOAD_SPECIALS]*ms, t[VLOAD_TEXTURES]*ms, t[VLOAD_EFFECT_SPRITES]*ms, t[VLOAD_SOUNDS]*ms,
		t[VLOAD_EFFECT_SOUNDS]*ms, t[VLOAD_FINISH]*ms);
	I_VitaTrace("[Vita] Load %s overlapping: file-read=%.0f ms (%u reads) pk3-unpack=%.0f ms (%u lumps) PNG-decode=%.0f ms sound-decode=%.0f ms\n",
		mapname, t[VLOAD_FILE_READ]*ms, VitaLoadReads, t[VLOAD_ZIP]*ms, VitaLoadZipLumps,
		t[VLOAD_PNG]*ms, t[VLOAD_SOUND_DECODE]*ms);
}
static unsigned VitaPNGDecodes;
unsigned I_VitaPNGDecodeCount() { return VitaPNGDecodes; }
void I_VitaPerfAdd(EVitaPerfStage stage, Uint64 start)
{
	// Asset decoding can run on the loader threads during a map load; their
	// time is reported by the parallel decode trace instead, and these
	// counters are only ever written by the game thread.
	if ((stage == VPERF_PNG_LOAD || stage == VPERF_SOUND_LOAD) && VitaGameThread != 0 && SDL_ThreadID() != VitaGameThread) return;
	if (stage == VPERF_PNG_LOAD) ++VitaPNGDecodes;
	if (start)
	{
		Uint64 elapsed = SDL_GetPerformanceCounter() - start;
		if (VitaLoadActive)
		{
			if (stage == VPERF_PNG_LOAD) VitaLoadTotals[VLOAD_PNG] += elapsed;
			else if (stage == VPERF_SOUND_LOAD) VitaLoadTotals[VLOAD_SOUND_DECODE] += elapsed;
		}
		if (!I_VitaPerfEnabled()) return;
		VitaPerfTotals[stage] += elapsed;
		VitaPerfCurrent[stage] += elapsed;
	}
}
void I_VitaPerfSprites(unsigned count)
{
	if (I_VitaPerfEnabled()) VitaPerfSpritesCurrent += count;
}
void I_VitaPerfFrame(Uint64 start)
{
	if (!start)
	{
		memset(VitaPerfTotals, 0, sizeof(VitaPerfTotals));
		memset(VitaPerfCurrent, 0, sizeof(VitaPerfCurrent));
		VitaPerfWindowStart = VitaPerfMaxFrame = 0;
		VitaPerfFrames = VitaPerfSlowFrames = VitaPerfSpritesCurrent = 0;
		VitaPerfSpritesTotal = VitaPerfSpritesPeak = VitaPerfSpritesWorst = 0;
		return;
	}
	Uint64 now = SDL_GetPerformanceCounter();
	Uint64 frequency = SDL_GetPerformanceFrequency();
	if (!VitaPerfWindowStart) VitaPerfWindowStart = start;
	Uint64 frame = now - start;
	if (frame > VitaPerfMaxFrame)
	{
		VitaPerfMaxFrame = frame;
		memcpy(VitaPerfWorst, VitaPerfCurrent, sizeof(VitaPerfWorst));
		VitaPerfSpritesWorst = VitaPerfSpritesCurrent;
	}
	if (frame > frequency / 30) ++VitaPerfSlowFrames;
	VitaPerfSpritesTotal += VitaPerfSpritesCurrent;
	if (VitaPerfSpritesCurrent > VitaPerfSpritesPeak)
		VitaPerfSpritesPeak = VitaPerfSpritesCurrent;
	VitaPerfSpritesCurrent = 0;
	memset(VitaPerfCurrent, 0, sizeof(VitaPerfCurrent));
	++VitaPerfFrames;
	double seconds = double(now - VitaPerfWindowStart) / frequency;
	if (seconds < 5.0) return;
	double ms = 1000.0 / frequency / VitaPerfFrames;
	I_VitaTrace("[Vita] Perf: %dx%d %.1f fps; game=%.2f display=%.2f render=%.2f convert=%.2f present=%.2f audio=%.2f ms/frame; max=%.2f ms\n",
		screen->GetWidth(), screen->GetHeight(), VitaPerfFrames / seconds,
		VitaPerfTotals[VPERF_GAME]*ms, VitaPerfTotals[VPERF_DISPLAY]*ms,
		VitaPerfTotals[VPERF_RENDER]*ms, VitaPerfTotals[VPERF_CONVERT]*ms, VitaPerfTotals[VPERF_PRESENT]*ms,
		VitaPerfTotals[VPERF_AUDIO]*ms, VitaPerfMaxFrame*1000.0/frequency);
	I_VitaTrace("[Vita] Effects: thinkers=%.2f sort=%.2f sprites=%.2f ms/frame; sprite passes avg=%.1f peak=%u; frames over 33.3ms=%u/%u\n",
		VitaPerfTotals[VPERF_THINKERS]*ms, VitaPerfTotals[VPERF_SPRITE_SORT]*ms,
		VitaPerfTotals[VPERF_SPRITE_DRAW]*ms, double(VitaPerfSpritesTotal)/VitaPerfFrames,
		VitaPerfSpritesPeak, VitaPerfSlowFrames, VitaPerfFrames);
	double tickms = 1000.0 / frequency;
	I_VitaTrace("[Vita] Worst frame: game=%.2f thinkers=%.2f render=%.2f sort=%.2f sprites=%.2f convert=%.2f present=%.2f audio=%.2f ms; sprite passes=%u\n",
		VitaPerfWorst[VPERF_GAME]*tickms, VitaPerfWorst[VPERF_THINKERS]*tickms,
		VitaPerfWorst[VPERF_RENDER]*tickms, VitaPerfWorst[VPERF_SPRITE_SORT]*tickms,
		VitaPerfWorst[VPERF_SPRITE_DRAW]*tickms, VitaPerfWorst[VPERF_CONVERT]*tickms,
		VitaPerfWorst[VPERF_PRESENT]*tickms, VitaPerfWorst[VPERF_AUDIO]*tickms,
		VitaPerfSpritesWorst);
	I_VitaTrace("[Vita] Asset decode: PNG=%.2f sound=%.2f ms/frame; worst-frame PNG=%.2f sound=%.2f ms\n",
		VitaPerfTotals[VPERF_PNG_LOAD]*ms, VitaPerfTotals[VPERF_SOUND_LOAD]*ms,
		VitaPerfWorst[VPERF_PNG_LOAD]*tickms, VitaPerfWorst[VPERF_SOUND_LOAD]*tickms);
	// Audio mixing moved to its own thread (multicore pass01), so its old
	// EffectMix/Synth stages are no longer recorded; report draw threads here.
	{
		FDrawerStats ds;
		R_DrawerTakeStats(ds);
		double perFrame = 1.0 / VitaPerfFrames;
		I_VitaTrace("[Vita] Draw threads: %s main-share=%d/8%s worker-split=%d/%d; wait=%.2f ms/frame (worst %.2f); worker busy=%.2f/%.2f ms/frame; commands=%.0f syncs=%.1f per frame\n",
			ds.active ? "on" : "off", ds.mainshare, ds.mainshareAuto ? " (auto)" : "", ds.worker1Percent, 100 - ds.worker1Percent,
			VitaPerfTotals[VPERF_DRAW_WAIT]*ms, VitaPerfWorst[VPERF_DRAW_WAIT]*tickms,
			ds.busyMicros[0]/1000.0*perFrame, ds.busyMicros[1]/1000.0*perFrame,
			ds.commands*perFrame, ds.syncs*perFrame);
	}
	// Game-thread time inside the 3D view (nested in render). With draw threads
	// on, these include submitting commands but not the workers' pixel work.
	I_VitaTraceMemory(NULL);
	I_VitaTrace("[Vita] Render stages: walls/BSP=%.2f planes=%.2f masked=%.2f ms/frame; worst-frame walls/BSP=%.2f planes=%.2f masked=%.2f draw-wait=%.2f ms\n",
		VitaPerfTotals[VPERF_R_WALLS]*ms, VitaPerfTotals[VPERF_R_PLANES]*ms, VitaPerfTotals[VPERF_R_MASKED]*ms,
		VitaPerfWorst[VPERF_R_WALLS]*tickms, VitaPerfWorst[VPERF_R_PLANES]*tickms, VitaPerfWorst[VPERF_R_MASKED]*tickms,
		VitaPerfWorst[VPERF_DRAW_WAIT]*tickms);
	memset(VitaPerfTotals, 0, sizeof(VitaPerfTotals));
	VitaPerfFrames = VitaPerfSlowFrames = VitaPerfSpritesTotal = VitaPerfSpritesPeak = 0;
	VitaPerfMaxFrame = 0; VitaPerfWindowStart = now;
}
#endif
