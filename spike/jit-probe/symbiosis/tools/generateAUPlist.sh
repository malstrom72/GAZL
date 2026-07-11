#!/usr/bin/env sh
set -eu

OUT=""
FROM_BINARY=""
FACTORY="SymbiosisAUFactory"
NAME="Sonic Charge: SymbiosisTest"
DESCRIPTION="Sonic Charge: SymbiosisTest"
TYPE="aufx"
SUBTYPE="TSTP"
MANUFACTURER="SYMB"
VERSION="65536"
TAGS="Effects,Utilities"
SYMBIOSIS_PLUGIN_TYPE=""
SYMBIOSIS_CATEGORIES=""

NAME_SET=0
DESCRIPTION_SET=0
TYPE_SET=0
SUBTYPE_SET=0
MANUFACTURER_SET=0
VERSION_SET=0
TAGS_SET=0

while [ "$#" -gt 0 ]; do
	case "$1" in
		--out) OUT="$2"; shift 2 ;;
		--fromBinary) FROM_BINARY="$2"; shift 2 ;;
		--factory) FACTORY="$2"; shift 2 ;;
		--name) NAME="$2"; NAME_SET=1; shift 2 ;;
		--description) DESCRIPTION="$2"; DESCRIPTION_SET=1; shift 2 ;;
		--type) TYPE="$2"; TYPE_SET=1; shift 2 ;;
		--subtype) SUBTYPE="$2"; SUBTYPE_SET=1; shift 2 ;;
		--manufacturer) MANUFACTURER="$2"; MANUFACTURER_SET=1; shift 2 ;;
		--version) VERSION="$2"; VERSION_SET=1; shift 2 ;;
		--tags) TAGS="$2"; TAGS_SET=1; shift 2 ;;
		--symbiosisPluginType) SYMBIOSIS_PLUGIN_TYPE="$2"; shift 2 ;;
		--symbiosisCategories) SYMBIOSIS_CATEGORIES="$2"; shift 2 ;;
		*)
			echo "generateAUPlist: unknown argument: $1" >&2
			exit 1
			;;
	esac
done

if [ -z "$OUT" ]; then
	echo "generateAUPlist: --out is required" >&2
	exit 1
fi

OUT_DIR=$(CDPATH= cd -- "$(dirname -- "$OUT")" && pwd)
mkdir -p "$OUT_DIR"

if [ -n "$FROM_BINARY" ]; then
	TMP_CPP=$(mktemp "$OUT_DIR/generateAUPlist_probe_XXXXXX.cpp")
	TMP_BIN=$(mktemp "$OUT_DIR/generateAUPlist_probe_XXXXXX")
	TMP_OUT=$(mktemp "$OUT_DIR/generateAUPlist_probe_XXXXXX.txt")

	cat > "$TMP_CPP" <<'CPP'
#include "src/SymbiosisABI.h"
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

typedef SymbiosisFactory* (SYMBIOSIS_CALL *CreateFactoryProc)(const SymbiosisLoaderInfo* loaderInfo
		, const SymbiosisFactoryInterface** outFactoryInterface, UInt32 errorTextMaxLength, UTF8Z* outErrorText);

static void fourCCToText(UInt32 value, char outText[5]) {
	outText[0] = (value >> 24) & 0xFFu;
	outText[1] = (value >> 16) & 0xFFu;
	outText[2] = (value >> 8) & 0xFFu;
	outText[3] = (value >> 0) & 0xFFu;
	outText[4] = 0;
	for (int i = 0; i < 4; ++i) {
		if (outText[i] < 32 || outText[i] > 126) {
			outText[i] = '?';
		}
	}
}

int main(int argc, char** argv) {
	if (argc != 2) {
		fprintf(stderr, "usage: probe <binary>\n");
		return 1;
	}
	void* module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
	if (module == 0) {
		fprintf(stderr, "dlopen failed\n");
		return 1;
	}
	CreateFactoryProc createFactory = reinterpret_cast<CreateFactoryProc>(dlsym(module, "symbiosisCreateFactory"));
	if (createFactory == 0) {
		fprintf(stderr, "symbiosisCreateFactory not found\n");
		dlclose(module);
		return 1;
	}
	SymbiosisLoaderInfo loaderInfo;
	memset(&loaderInfo, 0, sizeof(loaderInfo));
	loaderInfo.structVersion = 1;
	loaderInfo.maxSymbiosisVersion = 1;
	loaderInfo.adapterFormat = "AU";
	const SymbiosisFactoryInterface* factoryInterface = 0;
	char errorText[1024];
	memset(errorText, 0, sizeof(errorText));
	SymbiosisFactory* factory = createFactory(&loaderInfo, &factoryInterface, 1023, errorText);
	if (factory == 0 || factoryInterface == 0) {
		fprintf(stderr, "createFactory failed\n");
		dlclose(module);
		return 1;
	}
	if (factoryInterface->getPlugInCount(factory) == 0) {
		fprintf(stderr, "no plugins\n");
		factoryInterface->destroy(factory);
		dlclose(module);
		return 1;
	}
	const SymbiosisPlugInInfo* info = factoryInterface->getPlugInInfo(factory, 0);
	if (info == 0) {
		fprintf(stderr, "plugin info null\n");
		factoryInterface->destroy(factory);
		dlclose(module);
		return 1;
	}
	char subtype[5];
	char manufacturer[5];
	fourCCToText(info->plugInId, subtype);
	fourCCToText(info->vendorId, manufacturer);

	printf("DISPLAY_NAME\t%s\n", (info->displayName != 0 ? info->displayName : ""));
	printf("DISPLAY_VENDOR\t%s\n", (info->displayVendor != 0 ? info->displayVendor : ""));
	printf("PLUGIN_TYPE\t%u\n", info->plugInType);
	printf("CATEGORIES\t%u\n", info->plugInCategories);
	printf("SUBTYPE\t%s\n", subtype);
	printf("MANUFACTURER\t%s\n", manufacturer);
	printf("VERSION\t%u\n", info->versionHex);

	factoryInterface->destroy(factory);
	dlclose(module);
	return 0;
}
CPP

	c++ -std=c++17 -I. "$TMP_CPP" -ldl -o "$TMP_BIN"
	"$TMP_BIN" "$FROM_BINARY" > "$TMP_OUT"

	PLUGIN_TYPE_VALUE=""
	CATEGORY_MASK_VALUE=""
	while IFS="$(printf '\t')" read -r KEY VALUE; do
		case "$KEY" in
			DISPLAY_NAME) [ "$NAME_SET" = "1" ] || NAME="$VALUE" ;;
			DISPLAY_VENDOR)
				if [ "$NAME_SET" = "0" ] && [ -n "$VALUE" ] && [ -n "$NAME" ]; then
					NAME="$VALUE: $NAME"
				fi
				[ "$DESCRIPTION_SET" = "1" ] || DESCRIPTION="$NAME"
				;;
			PLUGIN_TYPE) PLUGIN_TYPE_VALUE="$VALUE" ;;
			CATEGORIES) CATEGORY_MASK_VALUE="$VALUE" ;;
			SUBTYPE) [ "$SUBTYPE_SET" = "1" ] || SUBTYPE="$VALUE" ;;
			MANUFACTURER) [ "$MANUFACTURER_SET" = "1" ] || MANUFACTURER="$VALUE" ;;
			VERSION)
				if [ "$VERSION_SET" = "0" ]; then
					# Symbiosis exposes aa.bb.cc as 0xAABBCC; AudioComponents version expects 0xAABBCC00.
					VERSION=$((VALUE << 8))
				fi
				;;
			*) ;;
		esac
	done < "$TMP_OUT"

	if [ -z "$SYMBIOSIS_PLUGIN_TYPE" ] && [ -n "$PLUGIN_TYPE_VALUE" ]; then
		SYMBIOSIS_PLUGIN_TYPE="$PLUGIN_TYPE_VALUE"
	fi
	if [ -z "$SYMBIOSIS_CATEGORIES" ] && [ -n "$CATEGORY_MASK_VALUE" ]; then
		SYMBIOSIS_CATEGORIES="$CATEGORY_MASK_VALUE"
	fi

	rm -f "$TMP_CPP" "$TMP_BIN" "$TMP_OUT"
fi

if [ -n "$SYMBIOSIS_PLUGIN_TYPE" ] && [ "$TYPE_SET" = "0" ]; then
	case "$SYMBIOSIS_PLUGIN_TYPE" in
		effect|1) TYPE="aufx" ;;
		midiEffect|2) TYPE="aumf" ;;
		instrument|3) TYPE="aumu" ;;
		midiProcessor|4) TYPE="aumi" ;;
		*)
			echo "generateAUPlist: unsupported --symbiosisPluginType '$SYMBIOSIS_PLUGIN_TYPE'" >&2
			exit 1
			;;
	esac
fi

if [ -n "$SYMBIOSIS_CATEGORIES" ] && [ "$TAGS_SET" = "0" ]; then
	TAGS=""
	CATEGORY_MASK="$SYMBIOSIS_CATEGORIES"
	addTag() {
		if [ -z "$TAGS" ]; then
			TAGS="$1"
		else
			TAGS="${TAGS},$1"
		fi
	}
	if [ $((CATEGORY_MASK & 0x01)) -ne 0 ]; then addTag "Synthesizers"; fi
	if [ $((CATEGORY_MASK & 0x02)) -ne 0 ]; then addTag "Samplers"; fi
	if [ $((CATEGORY_MASK & 0x04)) -ne 0 ]; then addTag "Drums"; fi
	if [ $((CATEGORY_MASK & 0x08)) -ne 0 ]; then addTag "Pianos"; fi
	if [ $((CATEGORY_MASK & 0x10)) -ne 0 ]; then addTag "Guitars"; fi
	if [ $((CATEGORY_MASK & 0x20)) -ne 0 ]; then addTag "Vocals"; fi
	if [ $((CATEGORY_MASK & 0x40)) -ne 0 ]; then addTag "Utilities"; fi
	if [ $((CATEGORY_MASK & 0x80)) -ne 0 ]; then addTag "Analyzers"; fi
	if [ $((CATEGORY_MASK & 0x100)) -ne 0 ]; then addTag "Channel Strips"; fi
	if [ $((CATEGORY_MASK & 0x200)) -ne 0 ]; then addTag "Delays"; fi
	if [ $((CATEGORY_MASK & 0x400)) -ne 0 ]; then addTag "Reverbs"; fi
	if [ $((CATEGORY_MASK & 0x800)) -ne 0 ]; then addTag "Equalizers"; fi
	if [ $((CATEGORY_MASK & 0x1000)) -ne 0 ]; then addTag "Dynamics"; fi
	if [ $((CATEGORY_MASK & 0x2000)) -ne 0 ]; then addTag "Pitch"; fi
	if [ $((CATEGORY_MASK & 0x4000)) -ne 0 ]; then addTag "Distortion"; fi
	if [ $((CATEGORY_MASK & 0x8000)) -ne 0 ]; then addTag "Modulation"; fi
	if [ $((CATEGORY_MASK & 0x10000)) -ne 0 ]; then addTag "Filters"; fi
	if [ $((CATEGORY_MASK & 0x20000)) -ne 0 ]; then addTag "Spatial"; fi
	if [ $((CATEGORY_MASK & 0x40000)) -ne 0 ]; then addTag "Restoration"; fi
fi

if [ -z "$DESCRIPTION" ]; then
	DESCRIPTION="$NAME"
fi
if [ -z "$TAGS" ]; then
	TAGS="Effects"
fi

TAGS_XML=""
OLD_IFS=${IFS}
IFS=','
for TAG in $TAGS; do
	IFS=${OLD_IFS}
	TRIMMED=$(printf "%s" "$TAG" | sed 's/^ *//;s/ *$//')
	if [ -n "$TRIMMED" ]; then
		TAGS_XML="${TAGS_XML}				<string>${TRIMMED}</string>
"
	fi
	IFS=','
done
IFS=${OLD_IFS}

cat > "$OUT" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>AudioComponents</key>
	<array>
		<dict>
			<key>description</key>
			<string>${DESCRIPTION}</string>
			<key>factoryFunction</key>
			<string>${FACTORY}</string>
			<key>manufacturer</key>
			<string>${MANUFACTURER}</string>
			<key>name</key>
			<string>${NAME}</string>
			<key>subtype</key>
			<string>${SUBTYPE}</string>
			<key>type</key>
			<string>${TYPE}</string>
			<key>version</key>
			<integer>${VERSION}</integer>
			<key>tags</key>
			<array>
${TAGS_XML}			</array>
		</dict>
	</array>
</dict>
</plist>
PLIST

echo "generateAUPlist: OK ($OUT)"
