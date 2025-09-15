#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"

if [ -z "${TMPDIR:-}" ]; then
        TMPDIR="/tmp/"
fi

cd "${TMPDIR}"
curl -fL https://github.com/malstrom72/PikaScript/releases/latest/download/PikaCmdSourceDistribution.tar.gz -o PikaCmdSourceDistribution.tar.gz
mkdir -p "PikaCmd"
cd "PikaCmd"
rm -rf "SourceDistribution" || true
tar -xzf "../PikaCmdSourceDistribution.tar.gz"
rm -f PikaCmdSourceDistribution.tar.gz
cd "SourceDistribution"
bash BuildPikaCmd.sh
bash InstallPika.sh
cd ..
rm -rf "SourceDistribution"
echo SUCCESS
