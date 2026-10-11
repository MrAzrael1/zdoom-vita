#!/usr/bin/env bash
# Pinned VitaSDK dependency; leaves the installed SDK unchanged.
set -euo pipefail
: "${VITASDK:=/usr/local/vitasdk}"
export VITASDK
export PATH="$VITASDK/bin:$PATH"
dep_root="${1:-/tmp/zdoom-vita-deps}"
revision=918f2107ea72c2765c29d9ee6f78d19b36374ed8
archive="$dep_root/fluidsynth-lite.tar.gz"
mkdir -p "$dep_root"
curl -fsSL "https://codeload.github.com/fgsfdsfgs/fluidsynth-lite/tar.gz/$revision" -o "$archive"
printf '%s  %s\n' 2a0ab51bbfb7dc575f9783babd4d0494c3710af158acce8d22df4df784941f6d "$archive" | sha256sum -c -
tar -xzf "$archive" -C "$dep_root"
source_dir="$dep_root/fluidsynth-lite-$revision"
make -C "$source_dir" -f Makefile.vita -j4
printf 'CMake arguments:\n-DFLUIDSYNTH_INCLUDE_DIR=%s/include\n-DFLUIDSYNTH_LIBRARIES=%s/libfluidsynth.a\n' "$source_dir" "$source_dir"