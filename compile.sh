#!/bin/bash

src=main.c
avr-gcc -o "${src/.c/.elf}" -Wall -g -mmcu=attiny84 "$src"
