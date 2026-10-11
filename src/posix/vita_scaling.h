// Shared Vita rendering presets; physical output always remains 960x544.
#ifndef ZDOOM_VITA_SCALING_H
#define ZDOOM_VITA_SCALING_H
struct VitaRenderSize { int width, height; };
static const VitaRenderSize VitaRenderSizes[] = {
	{480,272}, {600,340}, {720,408}, {960,544}
};
inline VitaRenderSize I_VitaRenderSize(int preset)
{
	if (preset < 0 || preset >= 4) preset = 0;
	return VitaRenderSizes[preset];
}
#endif
