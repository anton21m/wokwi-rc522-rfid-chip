# SPDX-FileCopyrightText: © 2022 Uri Shaked <uri@wokwi.com>
# SPDX-License-Identifier: MIT

clean:
	echo "Cleaning up..."
	rm -rf build
	mkdir build
	chmod 777 build

compile-chip: clean
	DOCKER_HOST=unix:///var/run/docker.sock docker build -t arduino-chip "./"
	echo "Compiling chip.wasm..."
	DOCKER_HOST=unix:///var/run/docker.sock docker run --rm -v ./build:/out arduino-chip cp /tmp/chip.wasm /out/chip.wasm
	cp src/chip.json build/chip.json
	echo "Done."

compile-arduino:
	arduino-cli compile --fqbn arduino:avr:uno MFRC522_chip_simulation.ino --output-dir build

all: compile-chip compile-arduino