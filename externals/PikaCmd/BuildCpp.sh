#!/bin/bash

CPP_COMPILER="${CPP_COMPILER:-g++}"
CPP_OPTIONS="${CPP_OPTIONS:-}"
CPP_TARGET="${CPP_TARGET:-release}"
CPP_MODEL="${CPP_MODEL:-native}"

# Parsing build target and model
if [[ "$1" =~ ^(debug|beta|release)$ ]]; then
	CPP_TARGET="$1"
	shift
fi

if [[ "$1" =~ ^(x64|x86|arm64|native|fat)$ ]]; then
	CPP_MODEL="$1"
	shift
fi

# Setting compilation options based on the target
case "$CPP_TARGET" in
	debug) CPP_OPTIONS="-O0 -DDEBUG -g $CPP_OPTIONS" ;;
	beta) CPP_OPTIONS="-Os -DDEBUG -g $CPP_OPTIONS" ;;
	release) CPP_OPTIONS="-Os -DNDEBUG $CPP_OPTIONS" ;;
	*) echo "Unrecognized CPP_TARGET: $CPP_TARGET"; exit 1 ;;
esac

# Setting compilation options based on the model
case "$CPP_MODEL" in
    x64) CPP_OPTIONS="-m64 -arch x86_64 -target x86_64-apple-macos $CPP_OPTIONS" ;;
    x86) CPP_OPTIONS="-m32 -arch i386 -target i386-apple-macos $CPP_OPTIONS" ;;
    arm64) CPP_OPTIONS="-m64 -arch arm64 -target arm64-apple-macos $CPP_OPTIONS" ;;
    native) ;; # No additional flags needed for native compilation
    fat) CPP_OPTIONS="-m64 -arch x86_64 -arch arm64 $CPP_OPTIONS" ;;
    *) echo "Unrecognized CPP_MODEL: $CPP_MODEL"; exit 1 ;;
esac

CPP_OPTIONS="-fvisibility=hidden -fvisibility-inlines-hidden -Wno-trigraphs -Wreturn-type -Wunused-variable $CPP_OPTIONS"

if [ $# -lt 2 ]; then
	echo "BuildCpp.sh [debug|beta|release*] [x86|x64|arm64|native*|fat] <output> <source files and other compiler arguments>"
	echo "You can also use the environment variables: CPP_COMPILER, CPP_TARGET, CPP_MODEL and CPP_OPTIONS"
	exit 1
fi

echo "Compiling $1 $CPP_TARGET $CPP_MODEL using $CPP_COMPILER"
echo "$CPP_OPTIONS -o $@"

if ! $CPP_COMPILER -pipe $CPP_OPTIONS -o "$@" 2>&1; then
	echo "Compilation of $1 failed"
	exit 1
else
	echo "Compiled $1 successfully"
	exit 0
fi
