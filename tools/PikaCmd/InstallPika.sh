#!/bin/sh

set -e -u

chmod +x ./pika
if [ ! -d /usr/local ]; then
	sudo mkdir /usr/local
	sudo chown root:wheel /usr/local
	sudo chmod 755 /usr/local
fi
if [ ! -d /usr/local/bin ]; then
	sudo mkdir /usr/local/bin
	sudo chown root:wheel /usr/local/bin
	sudo chmod 755 /usr/local/bin
fi
sudo mv ./PikaCmd /usr/local/bin/
sudo cp ./pika /usr/local/bin/
sudo cp systools.pika /usr/local/bin/
