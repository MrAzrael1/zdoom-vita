#ifndef ZDOOM_VITA_INPUT_HELPERS_H
#define ZDOOM_VITA_INPUT_HELPERS_H
#include <cmath>
// ZDoom's console line is byte-based. Refuse text it cannot represent.
inline int I_VitaConsoleTextLength(const unsigned short *text)
{
	for (int i=0;i<=240;++i)
	{
		if (!text[i]) return i;
		if (text[i]<32 || text[i]>126) return -1;
	}
	return -1; // Missing terminator; never execute a truncated command.
}
inline int I_VitaTouchPixel(float normalized, int pixels)
{
	if (!std::isfinite(normalized) || normalized<=0) return 0;
	if (normalized>=1) return pixels-1;
	return int(normalized*pixels);
}
#endif
