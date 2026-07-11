#!/usr/bin/env sh
# Builds the JIT-probe plug-in as a VST3 bundle (universal arm64+x86_64) and optionally installs it.
# VST3 is the widest-coverage format across the target hosts (Ableton, Studio One, Cubase, REAPER,
# Bitwig, Waveform, ...). SymbiosisVST3.cpp #includes the VST3 SDK sources directly, so this is a single
# translation unit plus the shared core.
#
#   ./build_vst3.sh            # build into .build/
#   ./build_vst3.sh --install  # also copy to ~/Library/Audio/Plug-Ins/VST3
set -eu

. "$(dirname -- "$0")/build_common.sh"

INSTALL=0
[ "${1-}" = "--install" ] && INSTALL=1

if [ ! -d "$VST3_SDK_ROOT/pluginterfaces" ]; then
	echo "build_vst3: VST3 SDK not found at $VST3_SDK_ROOT (set VST3_SDK_ROOT)" >&2
	exit 1
fi

NAME="GazlJitProbe"
BUNDLE="$OUT_DIR/$NAME.vst3"
CONTENTS="$BUNDLE/Contents"
mkdir -p "$CONTENTS/MacOS"
EXECUTABLE="$CONTENTS/MacOS/$NAME"

echo "build_vst3: compiling (arm64+x86_64)"
c++ -std=c++17 $ARCHES $RELEASE_FLAGS $COMMON_INCLUDES -I"$VST3_SDK_ROOT" \
	-DRELEASE=1 \
	-bundle \
	"$SYMB_DIR/src/SymbiosisVST3.cpp" $CORE_SOURCES \
	-framework CoreFoundation -framework Cocoa \
	$PROBE_FRAMEWORKS \
	-o "$EXECUTABLE"

cat > "$CONTENTS/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key><string>English</string>
	<key>CFBundleExecutable</key><string>$NAME</string>
	<key>CFBundleIdentifier</key><string>com.gazl.jitprobe.vst3</string>
	<key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
	<key>CFBundleName</key><string>$NAME</string>
	<key>CFBundlePackageType</key><string>BNDL</string>
	<key>CFBundleShortVersionString</key><string>0.1.0</string>
	<key>CFBundleVersion</key><string>1</string>
	<key>LSMinimumSystemVersion</key><string>10.13</string>
</dict>
</plist>
PLIST
printf 'BNDL????' > "$CONTENTS/PkgInfo"

codesign --force --sign - --timestamp=none "$BUNDLE" >/dev/null 2>&1 || true

if [ "$INSTALL" = "1" ]; then
	DEST="$HOME/Library/Audio/Plug-Ins/VST3"
	mkdir -p "$DEST"
	rm -rf "$DEST/$NAME.vst3"
	cp -R "$BUNDLE" "$DEST/"
	echo "build_vst3: installed $DEST/$NAME.vst3"
fi

echo "build_vst3: OK ($BUNDLE)"
