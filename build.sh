#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"

mkdir -p output

# Build and test GAZLCmd beta
(cd tools && bash buildGAZLCmd.sh beta)
./output/GAZLCmdBeta

# Build GAZLCmd release
(cd tools && bash buildGAZLCmd.sh release)
cp output/GAZLCmd impala/GAZLCmd 2>/dev/null || cp output/GAZLCmd.exe impala/GAZLCmd.exe

# Build Impala
(cd tools && bash BuildImpala.sh)

# Run the Impala test suite from the source directory
 (cd impala && ../output/PikaCmd runTests.pika && (cd jspeg && node jspegCompilerTests.js && node runJspegTests.js))

# Optionally validate emitted .gazl metadata when requested
if [[ "${GAZL_VALIDATE:-0}" != 0 ]]; then
	declare -a gazl_files
	if [[ -n "${GAZL_VALIDATE_FILES:-}" ]]; then
		# shellcheck disable=SC2206  # word splitting is intentional for caller-supplied args
		gazl_files=( ${GAZL_VALIDATE_FILES} )
	else
		mapfile -t gazl_files < <(find impala/jspeg/testdata -type f -name '*.gazl' -print)
	fi
	if [[ ${#gazl_files[@]} -gt 0 ]]; then
		node tools/gazl-validate.js "${gazl_files[@]}"
	else
		echo "GAZL_VALIDATE enabled but no .gazl files were found for validation."
	fi
fi

# Verify the copied files by running the demo from the output directory
(cd output && ./PikaCmd impala.pika run ../impala/ImpalaDemo.impala)

