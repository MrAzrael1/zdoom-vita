# ZDoom Vita v0.11.0

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
      -DZDOOM_VITA_VERSION=00.11
    cmake --build build-vita --target zdoom.vpk-vpk -j4

Supply DOOM1.WAD, freedoom1.wad, and freedoom2.wad in ZDOOM_VITA_BUNDLED_IWAD_DIR.
Supply gzdoom.sf2 through ZDOOM_VITA_TEST_SOUNDFONT; it is currently packaged
as app0:/gm.sf2. These inputs are not stored in Git. The VPK includes README.txt.
The engine reports 2.8.1V; release naming is v0.11.0. Vita APP_VER uses 00.11.
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
