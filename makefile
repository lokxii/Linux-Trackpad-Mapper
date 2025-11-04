CC=gcc
CFLAGS=-g `pkg-config --cflags libevdev` -O2
LDFLAGS=`pkg-config --libs libevdev` -lX11 -lm

all: main

main: main.o
