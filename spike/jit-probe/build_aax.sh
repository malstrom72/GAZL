#!/usr/bin/env sh
# Builds the JIT-probe plug-in as an unsigned AAX .aaxplugin (universal arm64+x86_64) and optionally
# installs it for Pro Tools Developer (which loads unsigned dev builds; shipping PT needs a PACE-wrapped
# build). Stands SymbiosisAAX.cpp up against the in-repo AAX SDK purely for this probe.
#
#   ./build_aax.sh            # build into .build/
#   ./build_aax.sh --install  # also copy to /Library/Application Support/Avid/Audio/Plug-Ins
set -eu

. "$(dirname -- "$0")/build_common.sh"

INSTALL=0
[ "${1-}" = "--install" ] && INSTALL=1

if [ ! -f "$AAX_SDK_ROOT/Interfaces/AAX.h" ]; then
	echo "build_aax: AAX SDK not found at $AAX_SDK_ROOT (set AAX_SDK_ROOT)" >&2
	exit 1
fi
AAX_LIB="$AAX_SDK_ROOT/Libs/Release/libAAXLibrary_libcpp.a"
if [ ! -f "$AAX_LIB" ]; then
	echo "build_aax: AAX library not found at $AAX_LIB" >&2
	exit 1
fi

NAME="GazlJitProbeAAX"
BUNDLE="$OUT_DIR/$NAME.aaxplugin"
CONTENTS="$BUNDLE/Contents"
mkdir -p "$CONTENTS/MacOS"
EXECUTABLE="$CONTENTS/MacOS/$NAME"

echo "build_aax: compiling (arm64+x86_64)"
c++ -std=c++17 $ARCHES $RELEASE_FLAGS $COMMON_INCLUDES \
	-I"$AAX_SDK_ROOT" -I"$AAX_SDK_ROOT/Interfaces" -I"$AAX_SDK_ROOT/Interfaces/ACF" \
	-dynamiclib \
	"$SYMB_DIR/src/SymbiosisAAX.cpp" $CORE_SOURCES \
	"$AAX_LIB" \
	-framework CoreFoundation -framework CoreServices -framework Cocoa -framework Carbon -framework CoreAudio \
	$PROBE_FRAMEWORKS \
	-o "$EXECUTABLE"

printf 'TDMwPTul' > "$CONTENTS/PkgInfo"
cat > "$CONTENTS/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key><string>English</string>
	<key>CFBundleExecutable</key><string>$NAME</string>
	<key>CFBundleIdentifier</key><string>com.gazl.jitprobe.aax</string>
	<key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
	<key>CFBundleName</key><string>$NAME</string>
	<key>CFBundlePackageType</key><string>TDMw</string>
	<key>CFBundleShortVersionString</key><string>0.1.0</string>
	<key>CFBundleSignature</key><string>PTul</string>
	<key>CFBundleVersion</key><string>1</string>
	<key>LSMultipleInstancesProhibited</key><string>true</string>
	<key>NSAppleScriptEnabled</key><string>No</string>
</dict>
</plist>
PLIST

codesign --force --sign - --deep "$BUNDLE" >/dev/null 2>&1 || true

if [ "$INSTALL" = "1" ]; then
	DEST="/Library/Application Support/Avid/Audio/Plug-Ins"
	if mkdir -p "$DEST" 2>/dev/null && [ -w "$DEST" ]; then
		rm -rf "$DEST/$NAME.aaxplugin"
		cp -R "$BUNDLE" "$DEST/"
		echo "build_aax: installed $DEST/$NAME.aaxplugin"
	else
		echo "build_aax: cannot write $DEST (needs privileges). Copy manually:" >&2
		echo "  sudo cp -R \"$BUNDLE\" \"$DEST/\"" >&2
	fi
fi

echo "build_aax: OK ($BUNDLE)"
