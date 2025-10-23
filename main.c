#include <assert.h>
#include <fcntl.h>
#include <libevdev-1.0/libevdev/libevdev.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// x: [-5896, 6416], negative to left
// y: [-7363, 163], negative to up
typedef struct {
    int mod;  // non empty flag
    int x;
    int y;
} Touch;

#define error(format, ...)                                \
    do {                                                  \
        struct tm* lt = localtime(NULL);                  \
        char buf[1024];                                   \
        strftime(buf, 1024, "%Y-%m-%d %H:%M:%S", lt);     \
        fprintf(stderr, "[%s]" format, buf, __VA_ARGS__); \
    } while (0)

void libevdev_init(struct libevdev** ev_dev, int* fd) {
    *fd = open("/dev/input/event0", O_RDONLY);
    if (*fd == -1) {
        perror("open");
        exit(1);
    }

    int r = libevdev_new_from_fd(*fd, ev_dev);
    if (r != 0) {
        error("libevdev_new_from_fd: %s", strerror(-r));
        exit(1);
    }
}

void read_events(struct libevdev* dev, Touch touches[10]) {
    struct input_event ev;
    memset(touches, 0, sizeof(*touches) * 10);

    static int i = 0;
    while (1) {
        int r = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
        if (r != 0) {
            return;
        }
        if (ev.type == EV_SYN) {
            puts("---");
            if (ev.code != SYN_REPORT) {
                error("Non reporting EV_SYN %d\n", ev.code);
            }
            return;
        }

        switch (ev.code) {
            case ABS_MT_SLOT:
                assert(ev.value >= 0 && ev.value < 10);
                printf("SLOT: %d\n", ev.value);
                touches[i].mod = 1;
                break;

            case ABS_MT_POSITION_X:
                printf("X: %d\n", ev.value);
                touches[i].x = ev.value;
                break;

            case ABS_MT_POSITION_Y:
                printf("Y: %d\n", ev.value);
                touches[i].y = ev.value;
                break;
        }
    }
}

int main() {
    int fd;
    struct libevdev* dev;

    libevdev_init(&dev, &fd);

    // No way there is more than 10 touches
    Touch touches[10];
    while (1) {
        read_events(dev, touches);
    }

    libevdev_free(dev);
    return 0;
}
