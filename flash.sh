#!/bin/bash

src=main.c
echo "Compile..."
./compile.sh
echo "Flash..."
avrdude -c avrisp -b 19200 -p attiny84 -P /dev/ttyACM? -U flash:w:"${src/.c/.elf}"
