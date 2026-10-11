#include "posix/vita_input.h"
#include "v_video.h"

// Moved from sdl/i_system.cpp

#include <string.h>

#include <SDL.h>

#include "bitmap.h"
#include "v_palette.h"
#include "textures.h"

#ifdef ZD_VITA
static FTexture *VitaCursorTexture;
void I_VitaDrawTouchCursor()
{
	int x,y;
	if (!VitaCursorTexture || !screen || !I_VitaTouchPointer(x,y)) return;
	int size=screen->GetWidth()*24/960;
	screen->DrawTexture(VitaCursorTexture,x,y,DTA_DestWidth,size,DTA_DestHeight,size,TAG_DONE);
}
#endif
bool I_SetCursor(FTexture *cursorpic)
{
#ifdef ZD_VITA
	VitaCursorTexture=(cursorpic && cursorpic->UseType != FTexture::TEX_Null) ? cursorpic : TexMan["cursor"];
#endif
	static SDL_Cursor *cursor;
	static SDL_Surface *cursorSurface;

	if (cursorpic != NULL && cursorpic->UseType != FTexture::TEX_Null)
	{
		// Must be no larger than 32x32.
		if (cursorpic->GetWidth() > 32 || cursorpic->GetHeight() > 32)
		{
			return false;
		}

		if (cursorSurface == NULL)
			cursorSurface = SDL_CreateRGBSurface (0, 32, 32, 32, MAKEARGB(0,255,0,0), MAKEARGB(0,0,255,0), MAKEARGB(0,0,0,255), MAKEARGB(255,0,0,0));

		SDL_LockSurface(cursorSurface);
		BYTE buffer[32*32*4];
		memset(buffer, 0, 32*32*4);
		FBitmap bmp(buffer, 32*4, 32, 32);
		cursorpic->CopyTrueColorPixels(&bmp, 0, 0);
		memcpy(cursorSurface->pixels, bmp.GetPixels(), 32*32*4);
		SDL_UnlockSurface(cursorSurface);

		if (cursor)
			SDL_FreeCursor (cursor);
		cursor = SDL_CreateColorCursor (cursorSurface, 0, 0);
		SDL_SetCursor (cursor);
	}
	else
	{
		if (cursor)
		{
			SDL_SetCursor (NULL);
			SDL_FreeCursor (cursor);
			cursor = NULL;
		}
		if (cursorSurface != NULL)
		{
			SDL_FreeSurface(cursorSurface);
			cursorSurface = NULL;
		}
	}
	return true;
}
