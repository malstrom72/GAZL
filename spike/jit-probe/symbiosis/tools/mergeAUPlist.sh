#!/usr/bin/env sh
set -eu

if [ "$#" -ne 3 ]; then
	echo "usage: mergeAUPlist.sh <base_info_plist> <audio_components_plist> <out_info_plist>" >&2
	exit 1
fi

BASE_INFO_PLIST="$1"
AUDIO_COMPONENTS_PLIST="$2"
OUT_INFO_PLIST="$3"

cp "$BASE_INFO_PLIST" "$OUT_INFO_PLIST"

AUDIO_COMPONENTS_XML=$(plutil -extract AudioComponents xml1 -o - "$AUDIO_COMPONENTS_PLIST")

# Avoid external dependencies (rg) in Xcode shell phases.
if plutil -extract AudioComponents raw -o - "$OUT_INFO_PLIST" >/dev/null 2>&1; then
	plutil -remove AudioComponents "$OUT_INFO_PLIST"
fi
plutil -insert AudioComponents -xml "$AUDIO_COMPONENTS_XML" "$OUT_INFO_PLIST"

echo "mergeAUPlist: OK ($OUT_INFO_PLIST)"
