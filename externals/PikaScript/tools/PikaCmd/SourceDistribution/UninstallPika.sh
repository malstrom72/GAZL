#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"

sudo rm /usr/local/bin/PikaCmd
sudo rm /usr/local/bin/pika
sudo rm /usr/local/bin/systools.pika
