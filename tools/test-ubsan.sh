#!/usr/bin/env bash
# Run GAZL under UndefinedBehaviorSanitizer: the beta command-line tool, whose start-up runs the unit tests, and the
# replay of the committed fuzz corpus and fixed-crash inputs. Any undefined behaviour aborts the run.
#
# Needs a compiler with UBSan (clang++ or g++). CI runs this on Linux; locally, CPP_COMPILER picks the compiler. The
# .cmd twin only reports that MSVC has no UBSan.
set -e -o pipefail -u
cd "$(dirname "$0")/.."
mkdir -p output

: "${CPP_COMPILER:=clang++}"
UBSAN="-fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g ${CPP_OPTIONS:-}"		# CPP_OPTIONS adds flags

echo "test-ubsan: building with $CPP_COMPILER"
( cd tools && CPP_COMPILER="$CPP_COMPILER" CPP_OPTIONS="$UBSAN" \
		bash BuildCpp.sh beta native ../output/GAZLCmdUBSan -I.. GAZLCmd.cpp ../src/GAZL.cpp ../src/GAZLCpp.cpp )
( cd tools && CPP_COMPILER="$CPP_COMPILER" CPP_OPTIONS="$UBSAN" \
		bash BuildCpp.sh beta native ../output/GAZLReplayUBSan -DLIBFUZZ -DLIBFUZZ_STANDALONE -DGAZL_CANONICAL_NAN -I.. GAZLCmd.cpp GAZLReference.cpp ../src/GAZL.cpp )

echo "test-ubsan: unit tests"
./output/GAZLCmdUBSan > /dev/null

# Unpack into an EMPTY folder, as test-fuzz does, so leftovers from an earlier run are not replayed.
WORK=output/fuzz/ubsan
rm -rf "$WORK"
mkdir -p "$WORK"
tar -xzf tests/fuzz/textCorpus.tar.gz -C "$WORK"
LIST="$WORK/inputs.txt"
find "$WORK/corpus" -type f > "$LIST"
find tests/fuzz/textCrashes -type f -name '*.gazl' >> "$LIST"
N=$(wc -l < "$LIST" | tr -d ' ')

echo "test-ubsan: replaying $N inputs"
./output/GAZLReplayUBSan "@$LIST" > /dev/null 2> "$WORK/replay.log" || { tail -20 "$WORK/replay.log"; exit 1; }
echo "test-ubsan: $N inputs replayed, no undefined behaviour"
