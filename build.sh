#!/bin/sh
cc -std=c99 -s -O2 -Wall -Wextra -o mkcomp mkcomp.c -lX11 -lXcomposite -lXdamage -lXfixes -lXpresent -lGL
