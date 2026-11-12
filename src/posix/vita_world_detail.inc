// Included only by r_swrenderer.cpp on Vita.
#include "r_utility.h"
#include "st_stuff.h"
void R_DrawPlayerSprites();
#include "posix/vita_perf.h"
#include "posix/vita_debug.h"
#include "posix/vita_world_detail.h"

CUSTOM_CVAR(Int, vita_worlddetail, 0, CVAR_ARCHIVE)
{
 if(self<0)self=0;
 else if(self>2)self=2;
}
bool VitaRenderingLowWorld=false;

void FSoftwareRenderer::ReleaseVitaWorld()
{
 if(!VitaWorldCanvas)return;
 DSimpleCanvas *canvas=VitaWorldCanvas;VitaWorldCanvas=NULL;
 canvas->Destroy();canvas->ObjectFlags|=OF_YesReallyDelete;delete canvas;
}
FSoftwareRenderer::~FSoftwareRenderer(){ReleaseVitaWorld();}

// Always restore framebuffer geometry before drawing the weapon or UI, including
// recoverable rendering errors. The canvas stays locked until enlargement ends.
struct FVitaWorldContext {
 DSimpleCanvas *canvas;bool savedActive,restored;
 explicit FVitaWorldContext(DSimpleCanvas *target):
  canvas(target),savedActive(viewactive),restored(false){canvas->Lock();}
 void Restore(){
  if(restored)return;restored=true;
  VitaRenderingLowWorld=false;RenderTarget=screen;bRenderingToCanvas=false;
  viewactive=savedActive;
  R_ExecuteSetViewSize();
  screen->Lock(true);R_SetupBuffer();screen->Unlock();
  R_SetupFreelook();
 }
 ~FVitaWorldContext(){Restore();canvas->Unlock();}
};

bool FSoftwareRenderer::RenderVitaWorld(player_t *player)
{
 if(!vita_worlddetail || !viewactive || RenderTarget!=screen || bRenderingToCanvas)return false;
 const int percent=vita_worlddetail==1?75:50;
 const int width=std::max(160,screen->GetWidth()*percent/100);
 const int height=std::max(100,screen->GetHeight()*percent/100);
 const int statusHeight=ST_Y*height/screen->GetHeight();
 if(!VitaWorldCanvas || VitaWorldCanvas->GetWidth()!=width || VitaWorldCanvas->GetHeight()!=height){
  ReleaseVitaWorld();VitaWorldCanvas=new DSimpleCanvas(width,height);
  VitaWorldCanvas->ObjectFlags|=OF_Fixed;
  I_VitaTrace("[Vita] World detail: %d%% canvas=%dx%d framebuffer=%dx%d\n",
   percent,width,height,screen->GetWidth(),screen->GetHeight());
 }
 if(!VitaWorldCanvas->IsValid()){ReleaseVitaWorld();return false;}
 FVitaWorldContext context(VitaWorldCanvas);
 {
  FVitaPerfScope setup(VPERF_WORLD_SCALE);
  RenderTarget=VitaWorldCanvas;bRenderingToCanvas=true;VitaRenderingLowWorld=true;
  // Scale the full-screen geometry and status-bar height together. Using just
  // the cropped viewport as the aspect ratio would change FOV/status-bar views.
  R_SetWindow(setblocks,width,height,statusHeight);viewwindowx=viewwindowy=0;
  memset(VitaWorldCanvas->GetBuffer(),GPalette.BlackIndex,VitaWorldCanvas->GetPitch()*height);
 }
 R_RenderActorView(player->mo);
 const int sourceWidth=viewwidth,sourceHeight=viewheight;
 I_VitaPerfWorldSize(sourceWidth,sourceHeight);
 {
  FVitaPerfScope enlarge(VPERF_WORLD_SCALE);
  context.Restore();
  VitaWorldScaler.Blit(screen->GetBuffer()+viewwindowy*screen->GetPitch()+viewwindowx,
   screen->GetPitch(),viewwidth,viewheight,VitaWorldCanvas->GetBuffer(),
   VitaWorldCanvas->GetPitch(),sourceWidth,sourceHeight);
 }
 // The reduced pass suppressed PSprites. Draw them once using restored full-size
 // projection, preserving the normal deferred-2D weapon rendering path.
 R_DrawPlayerSprites();
 return true;
}
