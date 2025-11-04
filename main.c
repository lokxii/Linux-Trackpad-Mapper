#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <libevdev-1.0/libevdev/libevdev.h>
#include <linux/uinput.h>

#define TOUCH_MAX 20

// negative to left
float X_MIN, X_MAX;
#define X_RANGE (X_MAX - X_MIN)

// negative to up
float Y_MIN, Y_MAX;
#define Y_RANGE (Y_MAX - Y_MIN)

typedef struct {
    int down;
    int x;
    int y;
} Touch;

typedef struct {
    float w;
    float h;
} Geom;

typedef struct {
    int get_input_info;
    const char* input_event_path;
} Args;

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

const char* help_message =
    "%s [OPTIONS]\n"
    "   -h, --help\n"
    "       prints help message\n"
    "   -l, --list-devices\n"
    "       list /dev/input/* devices\n"
    "   -d, --device [path]\n"
    "       use [path] for trackpad event\n";

Args read_args(int argc, char** argv) {
    Args args = {0, "/dev/input/event0"};
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf(help_message, argv[0]);
            exit(0);
        }
        if (!strcmp(argv[i], "-l") || !strcmp(argv[i], "--list-devices")) {
            return (Args){.get_input_info = 1, NULL};
        }
        if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--device")) {
            i += 1;
            if (i >= argc) {
                error("Missing device path");
                exit(1);
            }
            args.input_event_path = argv[i];
        }
    }
    return args;
}

void list_devices() {
    DIR* dp;
    struct dirent* ep;

    dp = opendir("/dev/input/");
    if (!dp) {
        error("opendir(\"/dev/input/\"): %s", strerror(errno));
        exit(1);
    }

    char buf[1024] = {0};
    for (struct dirent* ep; (ep = readdir(dp));) {
        sprintf(buf, "/dev/input/%s", ep->d_name);

        int fd = open(buf, O_RDONLY);
        if (fd == -1) {
        }

        struct libevdev* dev;
        int r = libevdev_new_from_fd(fd, &dev);
        if (r != 0) {
            continue;
        }

        printf("/dev/input/%s\t%s\n", ep->d_name, libevdev_get_name(dev));
    }
}

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

void check_capability(struct libevdev* dev, const char* path) {
    if (!libevdev_has_event_type(dev, EV_KEY)) {
        error("Event type EV_KEY missing for %s", path);
        exit(1);
    }
    if (!libevdev_has_event_code(dev, EV_KEY, BTN_LEFT)) {
        error("Event code BTN_LEFT missing for %s", path);
        exit(1);
    }
    if (!libevdev_has_event_code(dev, EV_KEY, BTN_TOOL_DOUBLETAP)) {
        error("Event code BTN_TOOL_DOUBLETAP missing for %s", path);
        exit(1);
    }
    if (!libevdev_has_event_code(dev, EV_KEY, BTN_TOOL_TRIPLETAP)) {
        error("Event code BTN_TOOL_TRIPLETAP missing for %s", path);
        exit(1);
    }
    if (!libevdev_has_event_code(dev, EV_KEY, BTN_TOOL_QUADTAP)) {
        error("Event code BTN_TOOL_QUADTAP missing for %s", path);
        exit(1);
    }
    if (!libevdev_has_event_code(dev, EV_KEY, BTN_TOOL_QUINTTAP)) {
        error("Event code BTN_TOOL_QUINTTAP missing for %s", path);
        exit(1);
    }

    if (!libevdev_has_event_type(dev, EV_ABS)) {
        error("Event type EV_KEY missing for %s", path);
        exit(1);
    }
    if (!libevdev_has_event_code(dev, EV_ABS, ABS_MT_SLOT)) {
        error("Event code ABS_MT_SLOT missing for %s", path);
        exit(1);
    }
    if (!libevdev_has_event_code(dev, EV_ABS, ABS_MT_POSITION_X)) {
        error("Event code ABS_MT_POSITION_X missing for %s", path);
        exit(1);
    }
    if (!libevdev_has_event_code(dev, EV_ABS, ABS_MT_POSITION_Y)) {
        error("Event code ABS_MT_POSITION_Y missing for %s", path);
        exit(1);
    }
}

void init_trackpad(const char* path, struct libevdev** dev, int* fd) {
    *fd = open(path, O_RDONLY);
    if (*fd == -1) {
        printf("open(\"%s\"): %s", path, strerror(errno));
        exit(1);
    }

    int r = libevdev_new_from_fd(*fd, dev);
    if (r != 0) {
        error("libevdev_new_from_fd: %s", strerror(-r));
        exit(1);
    }

    check_capability(*dev, path);

    const struct input_absinfo* x_info =
        libevdev_get_abs_info(*dev, ABS_MT_POSITION_X);
    const struct input_absinfo* y_info =
        libevdev_get_abs_info(*dev, ABS_MT_POSITION_Y);
    if (!x_info) {
        error("Cannot query trackpad x axis range");
        exit(1);
    }
    if (!y_info) {
        error("Cannot query trackpad y axis range");
        exit(1);
    }

    X_MIN = x_info->minimum;
    X_MAX = x_info->maximum;
    Y_MIN = y_info->minimum;
    Y_MAX = y_info->maximum;
}

int init_uinput() {
    int fd = open("/dev/uinput", O_WRONLY);
    if (fd == -1) {
        error("open(\"/dev/uinput\"): %s", strerror(errno));
        exit(1);
    }

    static const int rel_list[] = {REL_X, REL_Y, REL_Z, REL_WHEEL, REL_HWHEEL};
    if (ioctl(fd, UI_SET_EVBIT, EV_REL)) {
        error("UI_SET_EVBIT %s failed\n", "EV_REL");
    }
    for (int i = 0; i < sizeof(rel_list) / sizeof(int); i++) {
        if (ioctl(fd, UI_SET_RELBIT, rel_list[i])) {
            error("UI_SET_RELBIT %d failed\n", i);
        }
    }

    if (ioctl(fd, UI_SET_EVBIT, EV_KEY)) {
        error("UI_SET_EVBIT %s failed\n", "EV_KEY");
    }
    if (ioctl(fd, UI_SET_KEYBIT, BTN_LEFT)) {
        error("UI_SET_KEYBIT %d failed\n", BTN_LEFT);
    }
    if (ioctl(fd, UI_SET_KEYBIT, BTN_RIGHT)) {
        error("UI_SET_KEYBIT %d failed\n", BTN_RIGHT);
    }

    static const struct uinput_setup usetup = {
        .name = "osu mouse",
        .id = {
            .bustype = BUS_VIRTUAL,
            .vendor = 0x2333,
            .product = 0x6666,
            .version = 1}};

    if (ioctl(fd, UI_DEV_SETUP, &usetup)) {
        error("UI_DEV_SETUP ioctl failed: %s", strerror(errno));
        exit(1);
    }

    if (ioctl(fd, UI_DEV_CREATE)) {
        error("UI_DEV_CREATE ioctl failed: %s", strerror(errno));
        exit(1);
    }

    return fd;
}

void read_events(
    struct libevdev* dev,
    Touch touches[TOUCH_MAX],
    int* click,
    int* touch_n) {
    struct input_event ev;

    static int i = 0;
    while (1) {
        int r = libevdev_next_event(dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
        if (r != 0) {
            return;
        }
        if (ev.type == EV_SYN) {
            if (ev.code != SYN_REPORT) {
                error("Non reporting EV_SYN %d", ev.code);
            }
            return;
        }
        if (ev.type == EV_KEY) {
            switch (ev.code) {
                case BTN_LEFT:
                    *click = ev.value;
                    break;

                case BTN_TOOL_DOUBLETAP:
                    *touch_n = ev.value ? 2 : 1;
                    memset(
                        &touches[*touch_n],
                        0,
                        (TOUCH_MAX - *touch_n) * sizeof(*touches));
                    break;

                case BTN_TOOL_TRIPLETAP:
                    *touch_n = ev.value ? 3 : 1;
                    memset(
                        &touches[*touch_n],
                        0,
                        (TOUCH_MAX - *touch_n) * sizeof(*touches));
                    break;

                case BTN_TOOL_QUADTAP:
                    *touch_n = ev.value ? 4 : 1;
                    memset(
                        &touches[*touch_n],
                        0,
                        (TOUCH_MAX - *touch_n) * sizeof(*touches));
                    break;

                case BTN_TOOL_QUINTTAP:
                    *touch_n = ev.value ? 5 : 1;
                    memset(
                        &touches[*touch_n],
                        0,
                        (TOUCH_MAX - *touch_n) * sizeof(*touches));
                    break;
            }
            return;
        }
        if (ev.type == EV_ABS) {
            switch (ev.code) {
                case ABS_MT_SLOT:
                    assert(ev.value >= 0 && ev.value < TOUCH_MAX);
                    i = ev.value;
                    touches[i].down = 1;
                    break;

                case ABS_MT_POSITION_X:
                    touches[i].x = ev.value - X_MIN;
                    break;

                case ABS_MT_POSITION_Y:
                    touches[i].y = ev.value - Y_MIN;
                    break;
            }
            return;
        }
    }
}

// Only interested in touches[0]
// Write the new coordinates on screen to *x and *y
int mouse_move(
    Touch touches[TOUCH_MAX],
    int touch_n,
    float* x,
    float* y,
    Geom screen) {
    size_t i = 0;

    const float X_ACCEPT_LOW = 0.65;
    const float X_ACCEPT_HIGH = 0.95;
    const float Y_ACCEPT_LOW = 0.1;
    const float Y_ACCEPT_HIGH = 0.4;
    float screen_ratio = screen.w / screen.h;

    // Ignore events outside range
    if (touches[i].x < X_RANGE * X_ACCEPT_LOW ||
        touches[i].x > X_RANGE * X_ACCEPT_HIGH ||
        touches[i].y < Y_RANGE * Y_ACCEPT_LOW ||
        touches[i].y > Y_RANGE * Y_ACCEPT_HIGH) {
        return 0;
    }

    *x = (touches[i].x - X_RANGE * X_ACCEPT_LOW) /
         (X_RANGE * (X_ACCEPT_HIGH - X_ACCEPT_LOW)) * screen.w;
    *y = (touches[i].y - Y_RANGE * Y_ACCEPT_LOW) /
         (Y_RANGE * (Y_ACCEPT_HIGH - Y_ACCEPT_LOW)) * screen.h;
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

void reset_mouse(int fd) {
    uinput_emit(fd, EV_REL, REL_X, INT32_MIN, 0);
    uinput_emit(fd, EV_REL, REL_Y, INT32_MIN, 1);
}

void emit_mouse_move_event(int fd, float x, float y) {
    static float X = 0, Y = 0;
    static float dx = 0, dy = 0;

    dx += x - X;
    dy += y - Y;

    int dx_i = (dx > 0 ? floor : ceil)(dx);
    int dy_i = (dy > 0 ? floor : ceil)(dy);
    uinput_emit(fd, EV_REL, REL_X, dx_i, 0);
    uinput_emit(fd, EV_REL, REL_Y, dy_i, 1);
    dx -= dx_i;
    dy -= dy_i;

    X = x;
    Y = y;
}

int main(int argc, char** argv) {
    int tfd;
    struct libevdev* trackpad;

    Args args = read_args(argc, argv);
    if (args.get_input_info) {
        list_devices();
        exit(0);
    }

    init_trackpad(args.input_event_path, &trackpad, &tfd);
    int ufd = init_uinput();
    sleep(1);
    reset_mouse(ufd);

    Geom screen = get_screen_geom();

    Touch touches[TOUCH_MAX];
    float x, y;
    int click = 0;
    int touch_n = 1;
    while (1) {
        int new_click = click;
        read_events(trackpad, touches, &new_click, &touch_n);

        if (click != new_click) {
            switch (touch_n) {
                case 1:
                    uinput_emit(ufd, EV_KEY, BTN_LEFT, new_click, 1);
                    break;

                case 2:
                    uinput_emit(ufd, EV_KEY, BTN_RIGHT, new_click, 1);
                    break;
            }
            click = new_click;
        }
        if (mouse_move(touches, touch_n, &x, &y, screen)) {
            emit_mouse_move_event(ufd, x, y);
        }
    }

    libevdev_free(trackpad);
    close(tfd);
    close(ufd);
    return 0;
}
