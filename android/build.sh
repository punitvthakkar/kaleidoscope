#!/bin/bash
# Builds the Android app. Usage: android/build.sh [--out NAME] [--release]
# Prints the APK path on the last line. Everything it uses is named here: nothing
# comes from the shell environment, an IDE, or a running Gradle daemon.
#
#   --out dev    builds "fractonaut dev" (app.fractonaut.dev), which installs beside
#                the released app with its own permissions and settings.
#   --out NAME   build into android/build/NAME (default: main), so two builds can
#                run side by side without touching each other's files.
#   --release    a release build for performance testing: minified, not debuggable,
#                no debug code, signed with the local debug key. Without it: the
#                debuggable build, with the debug code (-DFX_DEBUG) in dev builds.
#
# Steps: parameters and interface page (the Mac's generators) -> shaders extracted
# from index.html -> native library with the NDK's compiler -> Gradle packages it.
set -euo pipefail

# ---------------- pinned tools ----------------
SDK="$HOME/Library/Android/sdk"
NDK="$SDK/ndk/30.0.16248370"
TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/darwin-x86_64"
CXX="$TOOLCHAIN/bin/aarch64-linux-android31-clang++"     # minSdk 31
STRIP="$TOOLCHAIN/bin/llvm-strip"
JAVA_HOME="/opt/homebrew/opt/openjdk@26/libexec/openjdk.jdk/Contents/Home"
GRADLE="/opt/homebrew/bin/gradle"                          # 9.7.1
NODE="/opt/homebrew/bin/node"
for f in "$CXX" "$STRIP" "$JAVA_HOME/bin/java" "$GRADLE" "$NODE" "$SDK/platforms/android-36/android.jar"; do
  [ -e "$f" ] || { echo "build.sh: missing $f" >&2; exit 1; }
done

AND="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$AND")"
MAC="$ROOT/mac"
NAME=main
RELEASE=0
while [ $# -gt 0 ]; do
  case "$1" in
    --out) NAME="${2:?--out needs a name}"; shift 2 ;;
    --release) RELEASE=1; shift ;;
    *) echo "build.sh: unknown argument $1" >&2; exit 1 ;;
  esac
done
OUT="$AND/build/$NAME"
OBJ="$OUT/obj"
ASSETS="$OUT/assets"
LIBDIR="$OUT/jniLibs/arm64-v8a"
mkdir -p "$OBJ" "$ASSETS" "$LIBDIR"

# ---------------- generated sources and assets ----------------
"$NODE" "$MAC/tools/gen_params.mjs" "$ROOT/index.html" "$MAC/core/params_gen.h"
# The interface page: the Mac's page (mac/build/web) plus the Android adapter.
(cd "$MAC" && "$NODE" tools/make_ui.mjs)
rm -rf "$ASSETS/web" && cp -R "$MAC/build/web" "$ASSETS/web"
# Build steps; each fails loudly on its own.
#   tools/make_page.mjs     patches assets/web/index.html with the bridge adapter
#   tools/make_shaders.mjs  extracts the GLSL from index.html into assets/shaders
#   tools/make_stars.mjs    translates mac/render/shaders/stars.metal (the sky places) into assets/shaders
for step in make_page make_shaders make_stars; do
  [ -f "$AND/tools/$step.mjs" ] && "$NODE" "$AND/tools/$step.mjs" "$ROOT/index.html" "$ASSETS"
done
# The hand tracker's model (the one the web loads), fetched once into android/build/cache
# and checked against its pinned hash; it ships inside the APK, so the phone never
# downloads anything.
MODEL_URL="https://storage.googleapis.com/mediapipe-models/hand_landmarker/hand_landmarker/float16/1/hand_landmarker.task"
MODEL_SHA256="fbc2a30080c3c557093b5ddfc334698132eb341044ccee322ccf8bcf3607cde1"
MODEL="$AND/build/cache/hand_landmarker.task"
if [ ! -f "$MODEL" ]; then
  mkdir -p "$(dirname "$MODEL")"
  /usr/bin/curl -sSfL -o "$MODEL.part" "$MODEL_URL" || { echo "build.sh: cannot fetch $MODEL_URL" >&2; exit 1; }
  mv "$MODEL.part" "$MODEL"
fi
[ "$(/usr/bin/shasum -a 256 "$MODEL" | cut -d' ' -f1)" = "$MODEL_SHA256" ] || {
  echo "build.sh: $MODEL does not match its pinned sha256 (delete it to fetch again)" >&2; exit 1; }
cp "$MODEL" "$ASSETS/hand_landmarker.task"

# ---------------- native library ----------------
FLAGS=(-std=c++20 -O2 -flto -fPIC -fvisibility=hidden -fno-exceptions -fno-rtti
       -Wall -Wno-deprecated-declarations -DANDROID -I"$AND/native" -I"$MAC")
# Debug code (render/debug.cpp, the probes, fx.chambers / fx.skydbg) only in dev builds.
[ "$NAME" = dev ] && [ "$RELEASE" = 0 ] && FLAGS+=(-DFX_DEBUG)
SOURCES=("$MAC"/core/engine.cpp "$MAC"/core/fold.cpp "$MAC"/core/pens.cpp "$MAC"/core/hands.cpp "$MAC"/core/synth.cpp "$MAC"/core/looks.cpp "$MAC"/core/rmflight.cpp)
while IFS= read -r f; do SOURCES+=("$f"); done < <(find "$AND/native" -name '*.cpp' | sort)

pids=() objs=()
for src in "${SOURCES[@]}"; do
  obj="$OBJ/$(echo "${src#$ROOT/}" | tr / _).o"
  objs+=("$obj")
  "$CXX" "${FLAGS[@]}" -c "$src" -o "$obj" &
  pids+=($!)
done
for p in "${pids[@]}"; do wait "$p"; done

"$CXX" -shared -O2 -flto -static-libstdc++ -Wl,--gc-sections -Wl,--no-undefined -Wl,-z,max-page-size=16384 \
  "${objs[@]}" -o "$LIBDIR/libfractonaut.so" \
  -landroid -ljnigraphics -lnativewindow -llog -lEGL -lGLESv3 -laaudio -lmediandk -lz -lm
"$STRIP" --strip-unneeded "$LIBDIR/libfractonaut.so"

# ---------------- package ----------------
# Gradle reads fx.out for the library, the assets and its own build directory.
if [ -f "$AND/settings.gradle.kts" ]; then
  env -i HOME="$HOME" PATH="/usr/bin:/bin" JAVA_HOME="$JAVA_HOME" ANDROID_HOME="$SDK" \
    "$GRADLE" --no-daemon -q -p "$AND" --project-cache-dir "$OUT/gradle-cache" \
    -Pfx.out="$OUT" -Pfx.dev="$([ "$NAME" = dev ] && echo 1 || echo 0)" \
    ":app:assemble$([ "$RELEASE" = 1 ] && echo Release || echo Debug)" || {
      echo "build.sh: Gradle failed" >&2; exit 1; }
  if [ "$RELEASE" = 1 ]; then APK="$OUT/gradle/outputs/apk/release/app-release.apk"
  else APK="$OUT/gradle/outputs/apk/debug/app-debug.apk"; fi
  [ -f "$APK" ] || { echo "build.sh: no APK at $APK" >&2; exit 1; }
  echo "$APK"
else
  echo "build.sh: native library only (no Gradle project yet)" >&2
  echo "$LIBDIR/libfractonaut.so"
fi
