#!/usr/bin/env bash
# Builds impala/gazlVm.js, the self-contained GAZL VM the playground runs programs on.
# Needs the emsdk environment (source emsdk_env.sh first, or set EMCC to the em++ path).
set -e -o pipefail -u
cd "$(dirname "$0")"
EMCC=${EMCC:-em++}
"$EMCC" -O2 -DNDEBUG -fexceptions -I.. GAZLWasm.cpp ../src/GAZL.cpp \
	-o ../impala/gazlVm.js \
	-sSINGLE_FILE=1 -sWASM_ASYNC_COMPILATION=0 -sENVIRONMENT=web,node \
	-sALLOW_MEMORY_GROWTH=1 \
	-sEXPORTED_FUNCTIONS=_gazlRun,_gazlOut,_gazlErr \
	-sEXPORTED_RUNTIME_METHODS=ccall
