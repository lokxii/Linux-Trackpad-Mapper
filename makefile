CC=gcc
CFLAGS=-g `pkg-config --cflags libevdev`
LDFLAGS=`pkg-config --libs libevdev` -lX11

all: main

main: main.o
