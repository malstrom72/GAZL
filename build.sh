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
bash tools/BuildImpala.sh

# Run the Impala test suite from the source directory
(cd impala/jspeg && node jspegCompilerTests.js && node runJspegTests.js)

# Validate generated .gazl metadata for the JSPEG fixtures.
for gazl_file in impala/jspeg/testdata/*.expected.gazl; do
	case "$gazl_file" in
		impala/jspeg/testdata/externAssignment.expected.gazl|impala/jspeg/testdata/returnContractCaller.expected.gazl)
			continue
			;;
	esac
	node tools/gazl-validate.js "$gazl_file"
done
node tools/gazl-validate.js \
	impala/jspeg/testdata/returnContractCaller.expected.gazl \
	impala/jspeg/testdata/returnContractProviderFloat.expected.gazl

# Verify the staged Impala compiler by compiling with NuXJS and running with GAZLCmd.
./output/NuXJS -s output/impala.nuxjs.js \
	impala/ImpalaDemo.impala 0x4d2 impala/ImpalaDemo.impala > output/ImpalaDemo.gazl
./output/GAZLCmd output/ImpalaDemo.gazl main
