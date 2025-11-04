# Linux Trackpad Mapper

[This](https://github.com/lokxii/Mac-trackpad-mapper), but for linux.

Since I have moved to asahi linux, I wanted to play osu, but I need my trackpad
to map to absolute coordinate on screen. Setting libinput trackpad accel
profile to flat was not enough (I tried).

I gave up writing a UI for this utility, and thus this is a command line only
utility. Edit the function `mouse_move` to modify how the trackpad inputs are
mapped to.

Feel free to use any LLM to modify the code if you don't know how to.

## Building

Depedencies:
- X11 (works on wayland, but still required as a dependency to get screen
  geometry)
- libevdev


Compile by running:

```sh
make
```

## Usage

```
./main [OPTIONS]
   -h, --help
       prints help message
   -l, --list-devices
       list /dev/input/* devices
   -d, --device [path]
       use [path] for trackpad event
```

The program reads trackpad event from /dev/input/event0 by default, because
that is the default path on my computer. One can list all the available devices
using the `-l` flag or the `--list-devices` option, and specify a path using
`-d` flag or `--device` option.

There is a run script provided that I use personally. It assumes using sway and
the trackpad device name. Check and modify the run script when needed. Below
are the things required to run the program:

- Programs needs root privileges to read and write /dev/input/*
- Allow root to connect to X11 daemon by running `xhost local:root`
- Disable trackpad on sway or the WM you are using before running the program

