#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"

mkdir -p output

# Build and test GAZLCmd beta
(cd tools && bash buildGAZLCmd.sh beta)
./output/GAZLCmdBeta

# Build GAZLCmd release
(cd tools && bash buildGAZLCmd.sh release)

# Build Impala
bash tools/BuildImpala.sh

# Run the Impala test suite from the source directory
(cd impala && node jspegCompilerTests.js && node runJspegTests.js)

# Validate generated .gazl metadata for the JSPEG fixtures.
for gazl_file in impala/testdata/*.expected.gazl; do
	case "$gazl_file" in
		impala/testdata/externAssignment.expected.gazl|impala/testdata/returnContractCaller.expected.gazl)
			continue
			;;
	esac
	node tools/gazl-validate.js "$gazl_file"
done
node tools/gazl-validate.js \
	impala/testdata/returnContractCaller.expected.gazl \
	impala/testdata/returnContractProviderFloat.expected.gazl

# Verify the staged Impala compiler by compiling with NuXJS and running with GAZLCmd.
./output/NuXJS output/impala.nuxjs.js \
	impala/ImpalaDemo.impala output/ImpalaDemo.gazl 0x4d2 impala/ImpalaDemo.impala
./output/GAZLCmd output/ImpalaDemo.gazl main
