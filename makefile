CC=gcc
CFLAGS=-g `pkg-config --cflags libevdev`
LDFLAGS=`pkg-config --libs libevdev`

all: main

main: main.o
