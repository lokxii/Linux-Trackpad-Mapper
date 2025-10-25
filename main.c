#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <libevdev-1.0/libevdev/libevdev.h>
#include <linux/uinput.h>

#define TOUCHES_N 20

// negative to left
#define X_MIN -5896.0
#define X_MAX 6416.0
#define X_RANGE (X_MAX - X_MIN)

// negative to up
#define Y_MIN -7363.0
#define Y_MAX 163.0
#define Y_RANGE (Y_MAX - Y_MIN)

typedef struct {
    int mod;  // non empty flag
    int x;
    int y;
} Touch;

typedef struct {
    float w;
    float h;
} Geom;

#define error(...)                                    \
    do {                                              \
        time_t t = time(NULL);                        \
        struct tm* lt = localtime(&t);                \
        char buf[1024];                               \
        strftime(buf, 1024, "%Y-%m-%d %H:%M:%S", lt); \
        fprintf(stderr, "[%s] ", buf);                \
        fprintf(stderr, __VA_ARGS__);                 \
        fputc('\n', stderr);                          \
    } while (0)

Geom get_screen_geom() {
    char* wid;
    Display* display;
    Window w;
    int width, height, xscreen;
    if (!(display = XOpenDisplay(":0"))) {
        error("XOpenDisplay: %s", strerror(-errno));
        exit(1);
    }
    xscreen = DefaultScreen(display);
    width = DisplayWidth(display, xscreen);
    height = DisplayHeight(display, xscreen);

    XCloseDisplay(display);

    Geom screen = {
        .w = width,
        .h = height,
    };

    return screen;
}

void init_trackpad(struct libevdev** ev_dev, int* fd) {
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

int init_uinput() {
    int fd = open("/dev/uinput", O_WRONLY);

    if (ioctl(fd, UI_SET_EVBIT, EV_REL)) {
        error("UI_SET_EVBIT %s failed\n", "EV_REL");
    }

    static const int rel_list[] = {REL_X, REL_Y, REL_Z, REL_WHEEL, REL_HWHEEL};

    for (int i = 0; i < sizeof(rel_list) / sizeof(int); i++) {
        if (ioctl(fd, UI_SET_RELBIT, rel_list[i])) {
            error("UI_SET_RELBIT %d failed\n", i);
        }
    }

    static const struct uinput_setup usetup = {
        .name = "osu mouse",
        .id = {
            .bustype = BUS_VIRTUAL,
            .vendor = 0x2333,
            .product = 0x6666,
            .version = 1}};

    if (ioctl(fd, UI_DEV_SETUP, &usetup)) {
        perror("UI_DEV_SETUP ioctl failed");
        exit(1);
    }

    if (ioctl(fd, UI_DEV_CREATE)) {
        perror("UI_DEV_CREATE ioctl failed");
        exit(1);
    }

    return fd;
}

void read_events(struct libevdev* dev, Touch touches[TOUCHES_N]) {
    struct input_event ev;

    static int i = 0;
    while (1) {
        int r = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
        if (r != 0) {
            return;
        }
        if (ev.type == EV_SYN) {
            // puts("---");
            if (ev.code != SYN_REPORT) {
                error("Non reporting EV_SYN %d", ev.code);
            }
            return;
        }

        switch (ev.code) {
            case ABS_MT_SLOT:
                assert(ev.value >= 0 && ev.value < TOUCHES_N);
                // printf("SLOT: %d\n", ev.value);
                touches[i].mod = 1;
                break;

            case ABS_MT_POSITION_X:
                // printf("X: %d\n", ev.value);
                touches[i].x = ev.value - X_MIN;
                break;

            case ABS_MT_POSITION_Y:
                // printf("Y: %d\n", ev.value);
                touches[i].y = ev.value - Y_MIN;
                break;
        }
    }
}

// Only interested in touches[0]
int mouse_move(Touch touches[TOUCHES_N], int* x, int* y, Geom screen) {
#define Touch_eq(a, b) a.mod == b.mod&& a.x == b.x&& a.y == b.y
    static Touch t = {.mod = 1, .x = 0, .y = 0};
    const float X_ACCEPT_LOW = 0.65;
    const float X_ACCEPT_HIGH = 0.95;
    const float Y_ACCEPT_LOW = 0.1;
    const float Y_ACCEPT_HIGH = 0.65;
    float screen_ratio = screen.w / screen.h;

    if (!t.mod) {
        t.mod = 1;
        *x = t.x = X_RANGE * X_ACCEPT_LOW;
        *y = t.y = Y_RANGE * X_ACCEPT_LOW / screen_ratio;
        return 1;
    }

    if (Touch_eq(t, touches[0])) {
        return 0;
    }

    // Ignore events outside range
    if (touches[0].x < X_RANGE * X_ACCEPT_LOW ||
        touches[0].x > X_RANGE * X_ACCEPT_HIGH ||
        touches[0].y < Y_RANGE * Y_ACCEPT_LOW ||
        touches[0].y > Y_RANGE * Y_ACCEPT_HIGH) {
        return 0;
    }

    *x = (touches[0].x - X_RANGE * X_ACCEPT_LOW) /
         (X_RANGE * (X_ACCEPT_HIGH - X_ACCEPT_LOW)) * screen.w / 2;
    *y = (touches[0].y - Y_RANGE * Y_ACCEPT_LOW) /
         (Y_RANGE * (1 - Y_ACCEPT_HIGH - Y_ACCEPT_LOW)) * screen.h / 2;

    printf("%d %d\n", *x, *y);

    t = touches[0];

    return 1;
}

void uinput_emit(
    int fd,
    uint16_t type,
    uint16_t code,
    int32_t val,
    int syn_report) {
    struct input_event ie = {.type = type, .code = code, .value = val};

    write(fd, &ie, sizeof(ie));

    if (syn_report) {
        ie.type = EV_SYN;
        ie.code = SYN_REPORT;
        ie.value = 0;
        write(fd, &ie, sizeof(ie));
    }
}

void emit_mouse_move_event(int fd, int x, int y) {
    uinput_emit(fd, EV_REL, REL_X, x, 0);
    uinput_emit(fd, EV_REL, REL_Y, y, 1);
}

int main() {
    int tfd;
    struct libevdev* trackpad;

    init_trackpad(&trackpad, &tfd);
    int ufd = init_uinput();
    sleep(1);  // wait for wayland to recognize virtual device

    Geom screen = get_screen_geom();
    emit_mouse_move_event(ufd, INT32_MIN + 1, INT32_MIN + 1);

    Touch touches[TOUCHES_N];
    int x, y;
    while (1) {
        read_events(trackpad, touches);
        if (mouse_move(touches, &x, &y, screen)) {
            emit_mouse_move_event(ufd, INT32_MIN + 1, INT32_MIN + 1);
            emit_mouse_move_event(ufd, x, y);
        }
    }

    libevdev_free(trackpad);
    close(tfd);
    close(ufd);
    return 0;
}
