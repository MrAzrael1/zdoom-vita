#ifndef ZDOOM_HUD_SCALE_H
#define ZDOOM_HUD_SCALE_H

// Relative size of HUD elements; 1.0 preserves the original drawing path.
double V_HudScale();

// Statusbar-coordinate layouts are centered horizontally and anchored at the
// bottom. Apply the same transform to graphics, text and their clipping bounds.
inline void V_ScaleStatusBar(double &x, double &y, double &width, double &height,
 double screenWidth, double screenHeight, double scale)
{
 x = screenWidth * 0.5 + (x - screenWidth * 0.5) * scale;
 y = screenHeight + (y - screenHeight) * scale;
 width *= scale;
 height *= scale;
}
#endif
