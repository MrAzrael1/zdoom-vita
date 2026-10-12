# ZDoom Vita v0.12.0 testing candidate

The official user readme is [README.txt](README.txt). This document covers building
and maintaining the port. The original ZDoom 2.8.1 checkpoint is cf9d745.

## Architecture

Vita uses SDL2, the software renderer, and a queued stereo PCM mixer. The display
is 960x544; the default internal framebuffer is 480x272. Vita Settings exposes
render size, FPS display, and rear-touch weapon switching. MIDI/MUS music uses
FluidSynth-lite with the bundled SoundFont. Configuration and launcher selections
are saved under ux0:data/zdoom-vita/.

The launcher scans user IWADs under iwad/ and mods/patches under pwad/, including
subfolders. Packaged IWADs are read directly from app0:/iwad/. Full DOOM.WAD hides
redundant DOOM1.WAD choices. Strife VOICES.WAD is hidden from the IWAD list and
autoloaded beside its IWAD, with user-folder fallbacks. DeHackEd/BEX patches are
passed as -deh rather than archives.

## Dependencies

The current build was verified with VitaSDK 2026.08 (GCC 15.2), installed Vita
SDL2, CMake, and native host code-generation tools. Do not assume these are
installed on another machine. ASM, FMOD, OpenAL, and GTK are disabled.

Build the pinned FluidSynth-lite dependency with:

    bash tools/build_vita_fluidsynth.sh build-deps

This verifies the upstream archive checksum and prints the required CMake
include/library arguments. It does not modify the installed SDK.

The cross build imports native lemon, re2c, zipdir, updaterevision, arithchk,
and qnan targets through ImportExecutables.cmake. A clean native generator
bootstrap was verified in WSL:

    cmake -S . -B build-host -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS=-std=gnu17 \
      -DCMAKE_CXX_FLAGS=-std=gnu++11 -DNO_ASM=ON -DNO_FMOD=ON \
      -DNO_OPENAL=ON -DNO_GTK=ON \
      -DSDL2_INCLUDE_DIR=/usr/local/vitasdk/arm-vita-eabi/include/SDL2 \
      -DSDL2_LIBRARY=/usr/local/vitasdk/arm-vita-eabi/lib/libSDL2.a
    cmake --build build-host --target lemon re2c zipdir updaterevision qnan -j4

Only native generators are built; SDK SDL paths satisfy configuration and are
not used to link these host utilities.

## Build in the existing WSL environment

    export VITASDK=/usr/local/vitasdk
    export PATH="$VITASDK/bin:$PATH"
    cmake -S . -B build-vita -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
      -DIMPORT_EXECUTABLES="$PWD/build-host/ImportExecutables.cmake" \
      -DCMAKE_BUILD_TYPE=Release \
      -DNO_ASM=ON -DNO_FMOD=ON -DNO_OPENAL=ON -DNO_GTK=ON -DNO_STRIP=ON \
      -DDYN_FLUIDSYNTH=OFF \
      -DFLUIDSYNTH_INCLUDE_DIR="$PWD/build-deps/fluidsynth-lite-918f2107ea72c2765c29d9ee6f78d19b36374ed8/include" \
      -DFLUIDSYNTH_LIBRARIES="$PWD/build-deps/fluidsynth-lite-918f2107ea72c2765c29d9ee6f78d19b36374ed8/libfluidsynth.a" \
      -DZDOOM_VITA_BUNDLED_IWAD_DIR="$PWD/build-vita" \
      -DZDOOM_VITA_TEST_SOUNDFONT="$PWD/build-vita/gzdoom.sf2" \
      -DZDOOM_VITA_VERSION=00.12
    cmake --build build-vita --target zdoom.vpk-vpk -j4

Supply DOOM1.WAD, freedoom1.wad, and freedoom2.wad in ZDOOM_VITA_BUNDLED_IWAD_DIR.
Supply gzdoom.sf2 through ZDOOM_VITA_TEST_SOUNDFONT; it is currently packaged
as app0:/gm.sf2. These inputs are not stored in Git. The VPK includes README.txt.
The engine reports 2.8.1V; the local test version is v0.12.0. Vita APP_VER uses 00.12.
The linker fragment reserves space for Vita executable metadata.

## Assets and verification

The supplied DooM.ttf was rasterized offline into zdoom-vita-launcher-font.bin.
Regenerate it with tools/build_vita_launcher_font.py and host libfreetype.
ZDOOM_VITA_DOOM_FONT=OFF restores the original launcher font.
tools/vita_icon_indexed.py creates the required indexed-color Vita icon.

Tests in tests/ cover launcher scanning/handoff, input, configuration writing,
PCM mixing, and MIDI synthesis. They are maintained tests, not release artifacts.
Launcher tests use actual implementation with native API/SDL stubs and ASan/UBSan.
Real Vita testing remains necessary for graphics and audio behavior.
Preserve upstream copyright and license notices in the bundled libraries.

## v0.11.0

Selecting HEXDD.WAD locates the full HEXEN.WAD in the scanned IWAD catalog,
preferring the same folder, then the user IWAD root, then other subfolders.
The launcher passes the base path using -vita-base-iwad. The engine validates
the required game type and uses its existing Required/preload behavior, loading
Hexen before Deathkings while preserving Deathkings' game identity.
Hexen remains visible. Missing Hexen prevents launch and shows an explanation.

Launcher wording now uses "IWADS: UX0:data/zdoom-vita/iwad",
"MODS - SELECT LOAD ORDER", and "CIRCLE EXIT".

The former /tmp build folders were cleared externally. Current native tools,
dependency sources, and Vita build are in ignored build-host/, build-deps/, and
build-vita/ under the repository. The bundled IWADs and SoundFont were recovered
unchanged from the existing private v0.10.0 release. Host tools were bootstrapped
with CMAKE_POLICY_VERSION_MINIMUM=3.5, CMAKE_C_FLAGS=-std=gnu17 and
CMAKE_CXX_FLAGS=-std=gnu++11. lemon, re2c, zipdir, updaterevision, arithchk, and qnan are
built there; SDL2 include/library paths point at VitaSDK to satisfy configuration.

The user confirmed successful hardware testing and authorized publication of
v0.11.0. Launcher regression tests, engine dependency tests, and VPK integrity
checks passed.

## v0.12.0 testing candidate (unpublished)

The launcher now has IWADs, Mods, and Configs tabs. Select mods in the desired
order, open Configs, and press Cross on "Save current modlist". The Vita keyboard
uses the title "Set a name". Names keep only ASCII letters, digits, spaces, and underscores
(up to 64 characters); duplicate names are rejected without overwriting files.

Configs are plaintext files in ux0:data/zdoom-vita/configs. Selecting a saved
config applies its ordered modlist while preserving the current IWAD. A config
allows eight archives (WAD/PK3, including the existing PK7/ZIP formats) and one
DEH/BEX patch. Missing files, duplicates, and excessive file counts are rejected
without changing the current selection. An empty config clears selected mods.
Example format:

    # ZDoom Vita modlist v1
    mod=mods/example.pk3
    mod=balance.deh

Press Select anywhere in the launcher to edit extra launch arguments using the
native keyboard. Arguments support single or double quoted values, are appended
after the launcher-selected file arguments, and are remembered in launcher.cfg
when starting the game. They are passed directly to ZDoom without a shell.
Example: -skill 4 -warp 01 +name "Vita Player".
Use the IWAD tab to choose the game; the launcher's -iwad remains the first one.

Host regression tests cover saved mod order, multiple configs, naming, collision
protection, limits, missing files, keyboard cancellation, argument parsing and
persistence. Native keyboard presentation, three-tab layout, and launch behavior
remain subject to testing on real Vita hardware before publication.

### v0.12.0 second testing pass

Square deletes the highlighted saved config on the Configs tab. It does nothing
on the other tabs or on the save-new-config row. Deleting a saved config keeps
the currently selected mods. Start and the touchscreen Start Game button launch
the game. Config names support internal spaces; surrounding spaces are trimmed
and blank names are rejected. Existing configs remain compatible.

HUD investigation: HUD Options already includes Stretch status bar (st_scale)
and Stretch Fullscreen HUD (hud_scale). Both are boolean switches in ZDoom
2.8.1. SBARINFO mod scripts can force scaling in DSBarInfo::Draw, overriding
these preferences. Continuous custom HUD scaling would require changes to both
the fullscreen script coordinate/scale calculations and status bar positioning;
changing only the global display scale would also affect gameplay rendering.
A representative affected mod is needed to choose and verify the correct path.

### v0.12.0 HUD scaling test pass

Vita Settings now includes HUD scale (%) from 25 to 150 in steps of 5.
Default 100 preserves the previous rendering. The archived vita_hudscale
setting is independent of render resolution and the existing stretch switches.

The multiplier is applied in shared SBARINFO graphics/text drawing, including
forced scaling, clipping for bars, relative-center positioning and normal
statusbar-coordinate layouts. Built-in fullscreen elements using DTA_HUDRules
also receive the multiplier. The classic native status bar, crosshair, menus,
weapon sprites and messages rendered through other paths retain their existing
controls; this first implementation is not a blanket scale of every 2D layer.

GMOTA's SBARINFO was inspected as a test case: its 1024x768 layout forces
fullscreen scaling for both normal and fullscreen HUD states. There is no
mod-name detection, embedded mod config or bundled mod content.
tests/vita_hud_scale_test.py compiles the actual DrawGraphic/DrawString methods
against a recording canvas and checks anchored geometry, text, clipped bars,
independent stretching and statusbar layout under address/undefined sanitizers.
Hardware testing is required for appearance and usability.

### v0.12.0 fourth testing pass

The HUD size slider is also at the bottom of the stock HUD Options menu; both
entries use vita_hudscale. The explanatory text below the slider was removed.
SDL sound playback now respects snd_pitched, matching the original FMOD/OpenAL
backends. With randomization off, sound effects use their original sample rate;
with it on, the engine-provided pitch multiplier is applied. StartSound3D shares
the same playback path. The PCM mixer regression fixture covers both settings.

### v0.12.0 fifth testing pass

New configs use plaintext v2 with an iwad= field,
saving the selected IWAD alongside mods. Loading validates all files and the
IWAD before applying either; legacy modlist-only configs retain the current
IWAD. Selecting the active config again disables it and clears selected mods.
The active config name is persisted in launcher.cfg.

Mods displays direct child folders/files instead of flattening every subfolder.
Cross opens folders; holding Cross for 550 ms toggles the entire folder into
the ordered -file list, without entering it on release. Circle goes up a folder.
Folder entries are saved in configs and count toward the eight archive entries;
the one DEH/BEX entry limit does not inspect patches embedded inside archives
or folders. Catalog safety limits are 512 entries and 16 folder levels.
Host tests cover nested navigation, physical tap/hold/release, folder arguments,
IWAD restoration, active-config persistence/disable and backward compatibility.
Hardware testing remains required before publication.

### v0.12.0 sixth testing pass

Launcher footer: DPAD BROWSE  L/R CHANGE TAB  SELECT ARGS  START LAUNCH.
Mods instruction: X SELECT O BACK - HOLD X LOAD FOLDER.
Launcher instructions refer to X. Saving with an existing config name replaces
the IWAD and complete mod list, removing stale entries. A temporary write and
backup rename preserve the old file on replacement failure. Regression tests
cover successful overwrites, stale-entry removal and failed-rename rollback.

### v0.12.0 seventh testing pass

The Mods X/O instruction line is 95% of its previous font size.
Configs uses a left selection panel and right read-only details panel showing
the highlighted config's IWAD and ordered WAD/PK3/DEH/BEX/folder entries.
Highlighting the save row previews the current selection. Legacy configs label
the current IWAD explicitly. File reads are cached until focus or saved content
changes. Touches in the details panel do not activate config selection.

### v0.12.0 eighth testing pass

Every launcher startup sanitizes previous selections: selected mods (including
nested files), folders, active config and manual launch arguments are cleared.
Only the last valid IWAD is restored. This reset is also written to launcher.cfg
before interaction, so cancelling does not retain stale mod selections.
Named configs remain on disk and can be loaded explicitly each session.
Tests cover nested-file and whole-folder reset, explicit config activation,
same-session config/folder toggling, and stale -file argument removal.

### v0.12.0 ninth testing pass

Launcher banner includes 0.12.0. Vita config archiving now preserves live
normal and double-click binding tables around ArchiveGameData. The original
custom-key serializer marks matching actions and removes them from the live
tables; this was safe only for shutdown and broke repeated Vita autosaves.
The temporary state is restored after serialization, including exceptional
exit, while retaining the original per-mod section layout in zdoom.ini.

The inspected brutalv20b.pk3 defines Reload as reloader and Toss Grenade as
grenadetoss in BrutalDoomActions. Its BrutalDoomAdvMoves section contains roll
left/right actions. Correct entries for Square/Triangle in this specific file:

    [Doom.BrutalDoomActions.Bindings]
    pad_x=reloader
    pad_y=grenadetoss

Vita's physical X button uses pad_a; pad_x is Square and pad_y is Triangle.
No Brutal Doom content or special-case configuration is bundled.

### v0.12.0 tenth testing pass

Vita SDL sound effects now support Ogg Vorbis through a pinned stb_vorbis decoder.
Compressed effects are decoded once into the existing PCM cache, retaining
their sample rate and channel count; the mixer resamples during normal playback.
This is general format support without mod-name handling or converted mod assets.
WAV and original Doom PCM loading retain their existing paths.

Brutal Doom v20b's DSRIFLE, CHGNSHOT and DSRLAUN firing effects are Vorbis data,
which SDL_LoadWAV cannot load. tests/vita_ogg_audio_test.py accepts a private
mod path, extracts Ogg samples into a temporary folder and exercises the actual
SDL loader and mixer. All 203 supplied Ogg effects decode successfully; targeted
weapon effects produce non-silent PCM. Existing mixer regressions also pass.
Decoder source/commit/hash and full licensing are in src/thirdparty; the VPK
includes its license notice. No supplied mod data is bundled.
This addition covers Vorbis sound effects; compressed music playback and other
compressed codecs remain separate work.

### v0.12.0 eleventh testing pass: explosion profiling

Inspection of the private Brutal Doom v20b effects shows Rocket2's death state
spawns 27 ExplosionParticleHeavy, nine ExplosionParticleVeryFast, five flames
and five smoke actors, plus flares, crater detectors and other effects.
Further child spawns and grenade effects increase the workload. These are
DECORATE actors, not simply the engine's native particle pool.

Existing vita_profile logs now include time inside DThinker::RunThinkers,
sprite sorting and sprite drawing. Each five-second window retains the slowest
frame's stage breakdown, counts frames exceeding 33.3ms and reports average/peak
sprite draw passes. 3D-floor rendering may draw a sprite more than once, so
these counts are passes rather than unique actors. Timings overlap: thinkers
are included in game, and sprite sort/draw are included in render/display.
They must not be summed as independent frame costs. Sprite draw includes
clipping and any masked wall drawing performed there, not just blending.

No visual quality, actor behavior or resolution is changed. Test at 500MHz
with the same map/view: idle, a first explosion, then repeated explosions.
Enable vita_profile true in the console if the saved setting disabled it.
Retrieve the trace after at least five seconds following the final explosion.
Use stage evidence to choose between blend/overdraw, thinker and asset-cache
optimizations; no hardware FPS improvement is claimed for this diagnostic pass.
tests/vita_perf_test.py verifies worst-frame capture, reporting/window reset,
slow-frame counts and disable/re-enable behavior with a deterministic clock.

### v0.12.0 twelfth testing pass: PNG conversion candidate

The supplied pass11 hardware trace contains a 299.71ms gameplay frame:
108.39ms in thinkers, 135.82ms in sprite drawing, 0.07ms sorting.
A later 130.76ms frame contains 21.35ms thinkers and 64.51ms sprites.
Sorting is negligible. Startup/map loads and native keyboard periods are
excluded from gameplay conclusions. Stage times overlap, including audio
calls inside game/display, and must not be summed.

Vita RGB/RGBA/grayscale-alpha PNG palette conversion now reads source pixels
sequentially, shrinking the existing decoded buffer in place before using
the existing byte transpose. It allocates no additional memory and changes
neither RGB32k lookup nor the 128 alpha cutoff. Upstream conversion remains
the non-Vita path. Sanitized tests compare actual source implementations
across 420 format/dimension/transparency combinations, including keyed RGB
transparency, grayscale mapping, odd dimensions and alpha values 127/128.

PNG MakeTexture and compressed/WAV sound loading now have scope timers.
Asset decode lines show five-second averages and the slowest frame's decode
costs. They nest inside existing stages; no per-asset logging is added.
Fixed profiling log newline escaping from pass11.
This is a first-use conversion candidate, not a demonstrated solution to
ongoing effect overdraw or actor cost. Hardware validation remains required:
same map, view, resolution and 500MHz; first rocket/grenade then repeated shots,
with menus/keyboard closed during the measured periods.

### v0.12.0 thirteenth testing pass: bounded map-load prefetch

Pass12 hardware trace confirms first-use decoding is a major explosion hitch:
a 370.04ms gameplay frame includes 149.85ms PNG creation and 137.63ms sound
decoding. These overlap render/game stages; they are not additional costs.
Warm periods contain zero decoding and substantially lower frame times.
Different play sequences mean pass11/pass12 are not a controlled speed comparison.

vita_precacheeffects is archived and defaults true; exposed as Preload effects
in Vita Settings. It applies on map load, within the existing precache path.
The stock precache variable must also be enabled. It does not preload all mod
assets: Brutal v20b alone contains about 66MiB of decoded PNG sprite pixels.

After stock texture precaching, collect state sprite frames from actor classes
whose default render style is non-normal, without matching names or mod files.
Skip textures already covered by the stock hitlist, deduplicate texture IDs,
and load only pixels under a 32MiB pixel budget. Span creation remains lazy.
After stock sound cleanup, load additional concrete sounds (not random headers,
player placeholders, aliases or existing PCM). Limit source assets to 1MiB and
retained PCM to a conservative 16MiB budget, calculated at four bytes/frame.
Unload newly decoded overflow entries; preserve already cached sounds.
Object metadata, later sprite spans, decode scratch and ordinary caches are
outside these supplemental budgets. Assets not selected or beyond limits
remain available through the ordinary lazy loader. No damage, state timing,
effect density, resolution, MIDI settings or mod content is changed.
This targets first-use hitches; ongoing overdraw/thinker cost is unchanged.

tests/vita_preload_test.py runs extracted actual loops under ASan/UBSan:
style selection, repeated frame IDs, invalid sprite indices, existing hitlist,
exact/exceeded budgets, failed sounds, random/alias/player/oversized exclusions,
cache retention and disabled behavior. Hardware test should compare first and
repeated explosions after loading a map with Preload effects enabled; optionally
disable and reload the same map to compare. Read Preload count/budget lines and
Asset decode lines in boot-trace.log. Longer map loading is expected.

### v0.12.0 fourteenth testing pass: preload coverage and admission

Pass13 trace shows 1019 effect sprite textures (6651KiB) preloaded without
hitting the pixel budget, but sound preload filled 16378KiB with 132 concrete
sounds and skipped 254. The prior decode-then-reject policy wasted loading
time. Map load reached 18.19s, including 8.30s sound decode. Gameplay still
contains a 120.56ms sound decode and 51.28ms PNG load in separate slow frames.
A 22.7FPS effects-heavy window confirms ongoing workload also matters.
Different scenes are not a controlled before/after benchmark.

Preload candidates now prioritize later resource files (mods/overrides) and
smaller source effects within each file, before unused earlier-file sounds.
A metadata-only Vorbis probe reads duration/channels before PCM decoding and
reserves four bytes/frame (conservative for mono). Raw/DMX reserve a conservative
four bytes/source byte; WAV reserves sixteen bytes/source byte. Unknown/oversized
assets remain lazy. These admission estimates are checked before S_LoadSound,
with a final post-decode budget guard for malformed metadata. Resource reads
and decoder header setup still cost time; rejected Ogg samples are not fully
decoded then discarded. Supplemental budgets remain 32MiB pixel/16MiB PCM.

Sprite collection now includes default projectile and NOINTERACTION flags as
well as non-normal styles, and walks ancestor-owned states for each selected
class. It still deduplicates texture IDs and preserves the normal level hitlist.
There are no mod-specific names, effect removals, simulation changes or quality
reductions. Pending hardware check: map loading duration, Preload counts,
first/repeated explosions with console closed, Asset decode and warm FPS.

ASan/UBSan preload tests cover inherited projectile states and reject PCM budget
overflow before loading. All 203 supplied Vorbis assets pass the metadata budget
check against actual decoded lengths, and existing mixer regressions pass.

### v0.12.0 fifteenth testing pass: warm audio cost

Pass14 hardware trace: sprite preload increased from 1019/6651KiB to
3972/15832KiB; sound preload increased from 132 to 227 entries at the same
16MiB conservative PCM budget. Sound decode during map loading fell from
8.30s to 2.61s, but PNG preload rose from 2.60s to 8.54s. Total map-load frame
rose from 18.19s to 22.62s; broader coverage costs time. These are individual
observations, not controlled benchmarks. Heavy windows report 30.1-32.1FPS
and 86-104ms worst frames, with sound decoding zero. Warm worst frames still
show 40-49ms audio timing; renderer and thinkers remain significant too.

PCMStream now caches its fixed sample-rate/output-rate step instead of
recomputing it per output sample. Effect mixing hoists sample frame bounds,
channel count, buffer pointer and resampling step per voice/block, and keeps
the playback position local until the block finishes. Float gain arithmetic
order, loop semantics, mono/stereo handling, rates, 32 voices and MIDI settings
are unchanged. Existing mixer tests (including end-position callbacks, loops,
pitch toggle, stream lifetime, pause and gain) and 203 actual Vorbis effects pass.

Audio work trace lines report effect-mixing time and stream/synth callback
time, both as averages and for the worst frame. These nest inside Audio and
potentially Game/Display; do not sum overlapping stages. Synth timing includes
all PCM stream callbacks, not just FluidSynth. It does not include stream
sample conversion/resampling, which remains within Audio.
This is a modest hot-loop candidate plus profiling, not a claimed hardware
speedup or a map-loading speed fix. Test explosions at the same clock/map/view
with menus closed, and retain the new Audio work lines in boot-trace.log.

### v0.12.0 pass16: restore Vita Settings after mod menus

Menu finalization now checks the final OptionsMenu after all MENUDEF lumps have loaded and appends the Vita Settings link when missing. Existing stock links are retained without duplicates; mod controls remain intact. Both option-menu and list-menu descriptors are supported. Brutal Doom v20 replaces OptionsMenu completely and reproduces the missing-link cause.

Regression checks exercise missing/existing links, repeated finalization, mod-control preservation and list-menu placement. Hardware menu testing remains required. Further renderer/audio optimization is deferred to v0.13.0.

Profiling writes five trace lines about every five seconds, with synchronous open/write/close per line, and adds stage timing calls while enabled. Use vita_profile false for normal play; this archived setting disables performance instrumentation/reports, but not startup/error/config-save logs. Its overhead has not been measured on hardware.

### v0.12.0 pass17: launcher logging option

Added Options as the fourth launcher tab with a persistent Verbose Logging toggle, keyboard/controller/front-touch controls and default Off. The saved launcher choice applies after global engine CVars load so old profiling settings cannot override it. vita_profile also defaults false for launches that bypass the launcher. Console changes still work during play; the launcher choice is applied again next launcher run. Host launcher regression checks cover default-off behavior, saved On/Off, tab navigation and touch under AddressSanitizer/UndefinedBehaviorSanitizer. Hardware UI testing remains pending.

### v0.12.0 pass18: engine startup messages on screen

A temporary SDL display shows text received from I_PrintStr after the existing color-code stripping, independently of vita_profile. Vita trace output is not routed to this screen. It uses the launcher fixed-width fallback glyphs, wraps at 76 characters, retains 23 lines, joins fragmented print calls and limits redraws to 10 Hz. No progress percentage is invented. The temporary renderer and window are released before the engine SDL framebuffer creates its window; exception paths also release them. This display covers pre-framebuffer initialization; it is not a replacement for the engine loading screen after graphics take over. No extra diagnostic messages are added to the display.

Accepting valid launch arguments now clears the notice instead of substituting ARGS text for the normal IWAD/mod folder header. Argument parsing and handoff are unchanged. The host launcher harness passes fragment/wrap/scroll and cleanup regression checks under sanitizers, along with previous launcher tests. Vita build and VPK archive checks pass; real-device startup and graphics handoff still need testing.

### v0.12.0 pass19: native graphical startup experiment

Vita startup now adapts ZDoom 2.8.1 Windows startup content rather than native window controls. The original VGA ROM font draws the basic console, mod/IWAD title colors and real progress. Heretic uses the LOADING text/attributes and status/thermometer positions; Hexen decodes STARTUP planar artwork and packed NOTCH/NETNOTCH graphics; Strife crops STARTUP0 and animates original STRTP*/STRTLZ*/STRTBOT overlays. Game/default/explicit startup styles and -nostartup are honored. Missing/invalid graphical assets fall back to the console, and missing VGA font preserves the earlier scrolling display. Only original engine messages are routed here; tracing remains separate.

Hexen starts its orb song or the selected custom startup song, plays StartupTick and network-notch effects through the existing engine sound backend. Custom startup songs are also used with other styles when supplied. Audio is pumped at progress/status/message events; long startup operations without these events can still starve the queued mixer. Smooth continuous audio during all loading phases has not been established. No second audio device or unsafe background mixer was introduced. Startup-owned music stops on screen destruction; the engine retains its normal startup-done sound.

SDL texture presentation preserves artwork aspect ratio at 960x544 with black margins. Display refreshes are bounded except explicit status/final updates. Startup graphics close before engine framebuffer construction. Sanitizer tests cover actual adapter code with deterministic assets/audio hooks for all styles, invalid/missing resources, music ownership, planar/nibble ordering, progress bounds, clipping and launcher image presentation. Vita build/archive integrity checks pass. Real IWAD art/audio behavior requires hardware testing; Heretic text blink and multiplayer behavior are not fully reproduced/tested. Pass18 VPK is retained as the rollback candidate. No publication or version-number increase beyond v0.12.0.

### v0.12.0 pass21: classic Doom console commands

Added Vita-only missing classic Doom command spellings in m_cheat.cpp, routing to existing CHT handlers via DEM_GENERICCHEAT packets. Existing iddqd/idclev/idmus and idmypos CVar are not duplicated. iddt cycles am_cheat from the console; idbehold prints the original prompt plus command spellings. Permission and level-state guards prevent scheduling new gameplay cheats outside a level. Host sanitizer checks cover rejection gates, packet encoding, automap cycling and duplicate registration; Vita build/VPK checks pass. Actual inventory/power-up behavior remains the engine implementation and requires hardware confirmation.

### v0.12.0 pass22: directory audit and full-width console

Audited Selection::Arguments, FDirectory traversal, CheckEmbedded and FWadCollection::AddFile. Launcher folder handoff already matched upstream unpacked archive semantics. Nested ZIP/PK3 recognition is commented out in upstream CheckEmbedded; preserve this behavior rather than inventing a batch loader. Root WADs are embedded; namespaces/filtering/postprocess ordering remain the engine implementation.

Vita directory traversal now checks stat return values before using the structure, skips non-regular files, and preserves already-found resources when a child directory cannot be opened. Empty directories skip PostProcessArchive to avoid indexing an empty lump array. Regression tests run the real traversal/Open/CheckEmbedded code against nested resources, WAD/PK3 names, missing paths, a dangling link, a FIFO, a simulated unreadable child and an empty folder under sanitizers.

Startup image presentation gains an explicit full-width mode for the basic console: its 640x400 source now fills 960x544. Special art remains aspect preserved. Existing startup/launcher sanitizer tests verify full-width edge coverage and art borders. Vita build and VPK checks passed; hardware layout confirmation remains pending.
