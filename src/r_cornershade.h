// Corner shading: a cheap fake ambient occlusion for the software renderer.
//
// While walls are drawn, R_RenderSegLoop reports every screen column where a
// wall visibly meets its sector's floor or ceiling. After all walls, planes,
// skyboxes and mirrors of the view are drawn (and before sprites), the pixels
// on both sides of those seams are darkened with a short gradient whose
// length is a fixed distance in the world, so it shrinks with distance.
//
// The pass works on the finished 8-bit image through a few "darker by N
// steps" palette remap tables, and is queued as a banded command so the draw
// worker threads split it like the rest of the view.
#ifndef __R_CORNERSHADE_H__
#define __R_CORNERSHADE_H__

// True between R_CornerShadeBeginView and R_CornerShadeApply when enabled.
extern bool CornerShadeRecording;

// Start of R_RenderActorView (after R_SetupFrame and the queue window).
void R_CornerShadeBeginView();

// One seam column. seamY is the seam's true screen position, which may lie
// off screen or behind nearer geometry; only visible rows are shaded:
// floors:   wall rows from wallLimit (visible top of the wall) down to the
//           seam, floor rows from the seam down to planeLimit (floorclip)
// ceilings: ceiling rows from planeLimit (ceilingclip) down to the seam, wall
//           rows from the seam down to wallLimit (visible bottom of the wall)
// height is the camera's distance above the floor / below the ceiling and dy
// the seam's distance in rows from the horizon, both positive when visible.
// oppositeLimit is the other clip (ceilingclip for floors, floorclip for
// ceilings); together they bound the rows still uncovered in this column.
void R_CornerShadeAdd(int x, float seamY, int wallLimit, int planeLimit, int oppositeLimit, float height, float dy, bool ceiling);

// Before R_DrawMasked: queue the darkening pass and stop recording.
void R_CornerShadeApply();

#endif
