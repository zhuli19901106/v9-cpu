#!/usr/bin/env sh

#rm "$1.s" "$1"
./xc -o $1 -Iroot/lib "$1.c"
./dis $1 -o "$1.s"
