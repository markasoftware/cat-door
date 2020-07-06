#!/bin/bash

src=main.c
avr-gcc -o "${src/.c/.elf}" -Wall -O3 -mmcu=attiny84 "$src"
