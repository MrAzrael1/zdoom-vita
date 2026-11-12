// Vita diagnostic output independent of redirected C stdio.
#ifndef ZDOOM_VITA_DEBUG_H
#define ZDOOM_VITA_DEBUG_H
#ifdef ZD_VITA
#include <psp2/io/fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static inline void I_VitaTrace(const char *format, ...)
{
	char text[1024];
	va_list args;
	va_start(args, format);
	int length = vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	if (length <= 0) return;
	if (length >= (int)sizeof(text)) length = sizeof(text) - 1;
	SceUID file = sceIoOpen("ux0:data/zdoom-vita/boot-trace.log",
		SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
	if (file >= 0)
	{
		int written = 0;
		while (written < length)
		{
			int result = sceIoWrite(file, text + written, length - written);
			if (result <= 0) break;
			written += result;
		}
		sceIoClose(file);
	}
}
#endif
#endif
