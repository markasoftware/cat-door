#!/bin/bash

src=main.c
avr-gcc -o "${src/.c/.elf}" -Wall -O3 -DF_CPU=1000000 -mmcu=attiny84 "$src"
