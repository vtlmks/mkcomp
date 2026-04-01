#!/bin/sh
cc -std=c99 -O2 -Wall -Wextra -o mkcomp mkcomp.c \
	-lX11 -lXcomposite -lXdamage -lXfixes -lGL
