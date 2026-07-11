#!/usr/bin/env sh
# Builds the JIT-probe plug-in as a VST2 bundle (universal arm64+x86_64) and optionally installs it.
# VST2 is the most broadly-loadable legacy format (e.g. older host versions). SymbiosisVST2.cpp calls
# symbiosisCreateFactory directly and exports VSTPluginMain, so it links straight against the shared core.
#
#   ./build_vst2.sh            # build into .build/
#   ./build_vst2.sh --install  # also copy to ~/Library/Audio/Plug-Ins/VST
set -eu

. "$(dirname -- "$0")/build_common.sh"

INSTALL=0
[ "${1-}" = "--install" ] && INSTALL=1

if [ ! -f "$VST2_SDK_ROOT/pluginterfaces/vst2.x/aeffect.h" ]; then
	echo "build_vst2: VST2 SDK not found at $VST2_SDK_ROOT (set VST2_SDK_ROOT)" >&2
	exit 1
fi

NAME="GazlJitProbe"
BUNDLE="$OUT_DIR/$NAME.vst"
CONTENTS="$BUNDLE/Contents"
mkdir -p "$CONTENTS/MacOS"
EXECUTABLE="$CONTENTS/MacOS/$NAME"

echo "build_vst2: compiling (arm64+x86_64)"
c++ -std=c++17 $ARCHES $RELEASE_FLAGS $COMMON_INCLUDES -I"$VST2_SDK_ROOT" \
	-bundle \
	"$SYMB_DIR/src/SymbiosisVST2.cpp" $CORE_SOURCES \
	-framework CoreFoundation \
	$PROBE_FRAMEWORKS \
	-o "$EXECUTABLE"

cat > "$CONTENTS/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key><string>English</string>
	<key>CFBundleExecutable</key><string>$NAME</string>
	<key>CFBundleIdentifier</key><string>com.gazl.jitprobe.vst</string>
	<key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
	<key>CFBundleName</key><string>$NAME</string>
	<key>CFBundlePackageType</key><string>BNDL</string>
	<key>CFBundleShortVersionString</key><string>0.1.0</string>
	<key>CFBundleVersion</key><string>1</string>
	<key>CFBundleSignature</key><string>????</string>
	<key>LSMinimumSystemVersion</key><string>10.13</string>
</dict>
</plist>
PLIST
printf 'BNDL????' > "$CONTENTS/PkgInfo"

codesign --force --sign - --timestamp=none "$BUNDLE" >/dev/null 2>&1 || true

if [ "$INSTALL" = "1" ]; then
	DEST="$HOME/Library/Audio/Plug-Ins/VST"
	mkdir -p "$DEST"
	rm -rf "$DEST/$NAME.vst"
	cp -R "$BUNDLE" "$DEST/"
	echo "build_vst2: installed $DEST/$NAME.vst"
fi

echo "build_vst2: OK ($BUNDLE)"
