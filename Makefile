# SPDX-FileCopyrightText: © 2022 Uri Shaked <uri@wokwi.com>
# SPDX-License-Identifier: MIT

SOURCE_DIR := src
SOURCE_CHIP_NAME := spi-example
SOURCE_PROJECT := $(SOURCE_DIR)/spi-chip-example.ino

clean:
	echo "Cleaning up..."
	rm -rf build
	mkdir build
	chmod -R 777 build

compile-chip:
	cp $(SOURCE_DIR)/diagram.json diagram.json
	echo "Prepared to compile chip..."
	mkdir -p build/chip
	cp "$(SOURCE_DIR)/$(SOURCE_CHIP_NAME).chip.json" build/$(SOURCE_CHIP_NAME).json
	cp "$(SOURCE_DIR)/$(SOURCE_CHIP_NAME).chip.c" build/chip/main.c
	cp -R lib/* build/chip/
	echo "Compiling chip.wasm..."

	DOCKER_HOST=unix:///var/run/docker.sock docker build -t arduino-chip .
	DOCKER_HOST=unix:///var/run/docker.sock docker run --rm -v $(shell pwd)/build:/out arduino-chip cp /tmp/chip.wasm /out/chip.wasm
	mv ./build/chip.wasm ./build/$(SOURCE_CHIP_NAME).wasm 
	
	echo "Chip compiled successfully."

compile-arduino:
	mkdir -p ./build/sketch
	echo "Compiling Arduino sketch..."
	cp "$(SOURCE_PROJECT)" ./build/sketch/sketch.ino
	arduino-cli compile --fqbn arduino:avr:uno ./build/sketch --output-dir build


all: clean compile-chip compile-arduino