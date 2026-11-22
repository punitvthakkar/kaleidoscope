#!/bin/bash
# Builds mac/build/fractonaut.app. Usage: mac/build.sh [--run]
set -euo pipefail
MAC="$(cd "$(dirname "$0")" && pwd)"
cd "$MAC"
RUN=0 DEV=0
[ "${1:-}" = "--run" ] && RUN=1
# --dev: build "fractonaut dev" with its own bundle id (own permissions and settings)
# and install it in /Applications beside the released app.
[ "${1:-}" = "--dev" ] && DEV=1

node tools/gen_params.mjs ../index.html core/params_gen.h
node tools/make_ui.mjs

OBJ=build/obj
APP=build/fractonaut.app
[ "$DEV" = 1 ] && APP="build/fractonaut dev.app"
mkdir -p "$OBJ"
FLAGS=(-O2 -flto -mmacosx-version-min=26.0 -Wall -Wno-deprecated-declarations)
SOURCES=(core/engine.cpp core/fold.cpp core/pens.cpp core/hands.cpp core/synth.cpp core/looks.cpp core/rmflight.cpp
         render/renderer.mm platform/audio.mm platform/camera.mm platform/recorder.mm
         app/main.mm app/view.mm app/web.mm)

pids=() objs=()
for src in "${SOURCES[@]}"; do
  obj="$OBJ/$(echo "$src" | tr / _).o"
  objs+=("$obj")
  case "$src" in
    *.mm) clang++ -std=c++20 -fobjc-arc "${FLAGS[@]}" -c "$src" -o "$obj" & ;;
    *)    clang++ -std=c++20 "${FLAGS[@]}" -c "$src" -o "$obj" & ;;
  esac
  pids+=($!)
done
for p in "${pids[@]}"; do wait "$p"; done

clang++ "${FLAGS[@]}" "${objs[@]}" -o "$OBJ/fractonaut" \
  -framework Cocoa -framework WebKit -framework Metal -framework QuartzCore -framework CoreVideo \
  -framework CoreMedia -framework CoreImage -framework CoreGraphics -framework ImageIO -framework IOKit \
  -framework AVFoundation -framework AudioToolbox -framework CoreAudio -framework VideoToolbox \
  -framework Vision -framework CoreML

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$OBJ/fractonaut" "$APP/Contents/MacOS/fractonaut"
cp app/Info.plist "$APP/Contents/Info.plist"
if [ "$DEV" = 1 ]; then
  for kv in "CFBundleIdentifier app.fractonaut.mac.dev" "CFBundleName fractonaut dev" "CFBundleDisplayName fractonaut dev"; do
    /usr/libexec/PlistBuddy -c "Set :${kv%% *} ${kv#* }" "$APP/Contents/Info.plist"
  done
fi
cp -R build/web "$APP/Contents/Resources/web"
cp -R render/shaders "$APP/Contents/Resources/shaders"

# Icon: the neon sunburst (icons/make_icons.mjs), rebuilt when its source changes.
if [ ! -f build/AppIcon.icns ] || [ ../icons/mac-1024.png -nt build/AppIcon.icns ]; then
  SET=build/AppIcon.iconset; rm -rf "$SET"; mkdir -p "$SET"
  for s in 16 32 128 256 512; do
    sips -z $s $s ../icons/mac-1024.png --out "$SET/icon_${s}x${s}.png" >/dev/null
    sips -z $((s * 2)) $((s * 2)) ../icons/mac-1024.png --out "$SET/icon_${s}x${s}@2x.png" >/dev/null
  done
  iconutil -c icns "$SET" -o build/AppIcon.icns
  rm -rf "$SET"
fi
cp build/AppIcon.icns "$APP/Contents/Resources/AppIcon.icns"

codesign --force --sign - --options runtime --entitlements app/fractonaut.entitlements "$APP"
codesign --verify --strict "$APP"
if [ "$DEV" = 1 ]; then
  rm -rf "/Applications/fractonaut dev.app"
  cp -R "$APP" "/Applications/fractonaut dev.app"
  echo "/Applications/fractonaut dev.app"
  exit 0
fi
echo "$MAC/$APP"
[ "$RUN" = 1 ] && open "$APP"
exit 0
