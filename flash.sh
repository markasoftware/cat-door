#!/bin/bash

src="$1"
echo "Compile..."
avr-gcc -o "${src/.c/.elf}" -Wall -O3 -DF_CPU=1000000 -mmcu=attiny84 "$src"
echo "Flash..."
avrdude -c avrisp -b 19200 -p attiny84 -P /dev/ttyACM? -U flash:w:"${src/.c/.elf}"
