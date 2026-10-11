# ZDoom Vita v0.10.0

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

    bash tools/build_vita_fluidsynth.sh /tmp/zdoom-vita-deps

This verifies the upstream archive checksum and prints the required CMake
include/library arguments. It does not modify the installed SDK.

The cross build imports native lemon, re2c, zipdir, and updaterevision targets
through ImportExecutables.cmake. A clean native bootstrap on other host setups
has not yet been verified; the existing development host-tool build is used below.

## Build in the existing WSL environment

    export VITASDK=/usr/local/vitasdk
    export PATH="$VITASDK/bin:$PATH"
    cmake -S . -B /tmp/zdoom-vita-engine \
      -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
      -DIMPORT_EXECUTABLES=/tmp/zdoom-vita-native-tools/ImportExecutables.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DNO_ASM=ON -DNO_FMOD=ON -DNO_OPENAL=ON -DNO_GTK=ON -DNO_STRIP=ON \
      -DDYN_FLUIDSYNTH=OFF \
      -DFLUIDSYNTH_INCLUDE_DIR=/tmp/fluidsynth-lite-918f2107ea72c2765c29d9ee6f78d19b36374ed8/include \
      -DFLUIDSYNTH_LIBRARIES=/tmp/fluidsynth-lite-918f2107ea72c2765c29d9ee6f78d19b36374ed8/libfluidsynth.a \
      -DZDOOM_VITA_BUNDLED_IWAD_DIR=/tmp/zdoom-vita-engine \
      -DZDOOM_VITA_TEST_SOUNDFONT=/tmp/zdoom-vita-engine/gzdoom.sf2 \
      -DZDOOM_VITA_VERSION=00.10
    cmake --build /tmp/zdoom-vita-engine --target zdoom.vpk-vpk -j4

Supply DOOM1.WAD, freedoom1.wad, and freedoom2.wad in ZDOOM_VITA_BUNDLED_IWAD_DIR.
Supply gzdoom.sf2 through ZDOOM_VITA_TEST_SOUNDFONT; it is currently packaged
as app0:/gm.sf2. These inputs are not stored in Git. The VPK includes README.txt.
The engine reports 2.8.1V; release naming is v0.10.0. Vita APP_VER uses 00.10.
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
