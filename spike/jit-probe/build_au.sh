#!/usr/bin/env sh
# Builds the JIT-probe plug-in as an AU v2 .component (universal arm64+x86_64) and optionally installs
# it. AU is what Logic / GarageBand / MainStage load; needs no external SDK (system frameworks only).
#
#   ./build_au.sh            # build into .build/
#   ./build_au.sh --install  # also copy to ~/Library/Audio/Plug-Ins/Components
set -eu

. "$(dirname -- "$0")/build_common.sh"

INSTALL=0
[ "${1-}" = "--install" ] && INSTALL=1

COMPONENT_NAME="GazlJitProbe"
BUNDLE="$OUT_DIR/$COMPONENT_NAME.component"
CONTENTS="$BUNDLE/Contents"
mkdir -p "$CONTENTS/MacOS"
EXECUTABLE="$CONTENTS/MacOS/$COMPONENT_NAME"

echo "build_au: compiling (arm64+x86_64)"
c++ -std=c++17 $ARCHES $RELEASE_FLAGS $COMMON_INCLUDES \
	-bundle \
	"$SYMB_DIR/src/SymbiosisAU.mm" $CORE_SOURCES \
	-framework AppKit -framework AudioUnit -framework AudioToolbox -framework CoreAudio \
	$PROBE_FRAMEWORKS \
	-o "$EXECUTABLE"

# Base Info.plist.
cat > "$OUT_DIR/Info.base.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key><string>English</string>
	<key>CFBundleExecutable</key><string>$COMPONENT_NAME</string>
	<key>CFBundleIdentifier</key><string>com.gazl.jitprobe.au</string>
	<key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
	<key>CFBundleName</key><string>$COMPONENT_NAME</string>
	<key>CFBundlePackageType</key><string>BNDL</string>
	<key>CFBundleShortVersionString</key><string>0.1.0</string>
	<key>CFBundleVersion</key><string>1</string>
	<key>LSMinimumSystemVersion</key><string>10.13</string>
	<key>NSHumanReadableCopyright</key><string>GAZL JIT spike A1 probe.</string>
</dict>
</plist>
PLIST

# generateAUPlist dlopens the binary and reads plugInId/vendorId; it needs CWD = the symbiosis dir
# (its internal probe #includes "src/SymbiosisABI.h").
( cd "$SYMB_DIR" && "$SYMB_DIR/tools/generateAUPlist.sh" \
	--out "$OUT_DIR/AudioComponents.generated.plist" \
	--fromBinary "$EXECUTABLE" \
	--factory "SymbiosisAUFactory" \
	--name "GAZL: JIT Probe" )

"$SYMB_DIR/tools/mergeAUPlist.sh" \
	"$OUT_DIR/Info.base.plist" "$OUT_DIR/AudioComponents.generated.plist" "$CONTENTS/Info.plist"

# Ad-hoc sign so hosts that validate will load it.
codesign --force --sign - --timestamp=none "$BUNDLE" >/dev/null 2>&1 || true

if [ "$INSTALL" = "1" ]; then
	DEST="$HOME/Library/Audio/Plug-Ins/Components"
	mkdir -p "$DEST"
	rm -rf "$DEST/$COMPONENT_NAME.component"
	cp -R "$BUNDLE" "$DEST/"
	echo "build_au: installed $DEST/$COMPONENT_NAME.component"
fi

echo "build_au: OK ($BUNDLE)"
