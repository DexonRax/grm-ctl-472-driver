#include <stdio.h>
#include <stdint.h>
#include <hidapi/hidapi.h>

// X11 / XTest headers
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>

// --- Configuration Struct ---
typedef struct {
    int pos_x;          // Tablet active area offset X
    int pos_y;          // Tablet active area offset Y
    int size_x;         // Tablet active area width
    int size_y;         // Tablet active area height
    int rot;            // Rotation (0=0, 1=90, 2=180, 3=270)

    // Target Monitor Settings
    int screen_x;       // Monitor Top-Left X (e.g., 0, 1920, -1920)
    int screen_y;       // Monitor Top-Left Y (usually 0)
    int screen_w;       // Monitor Width (e.g., 1920)
    int screen_h;       // Monitor Height (e.g., 1080)
} TabletConfig;

#define MAX_TABLET_X 15200
#define MAX_TABLET_Y 9500

// Global X11 display handle
static Display *g_display = NULL;

TabletConfig load_config() {
    // Default: Map full tablet to a standard 1080p primary monitor
    TabletConfig cfg = {0, 0, MAX_TABLET_X, MAX_TABLET_Y, 0, 0, 0, 1920, 1080};

    FILE *f = fopen("config.txt", "r");
    if (f) {
        if (fscanf(f, "POS_X=%d\nPOS_Y=%d\nSIZE_X=%d\nSIZE_Y=%d\nROT=%d\nSCREEN_X=%d\nSCREEN_Y=%d\nSCREEN_W=%d\nSCREEN_H=%d",
            &cfg.pos_x, &cfg.pos_y, &cfg.size_x, &cfg.size_y, &cfg.rot,
            &cfg.screen_x, &cfg.screen_y, &cfg.screen_w, &cfg.screen_h) == 9) {

            printf("Mapping to Monitor at (%d,%d) size %dx%d\n",
                cfg.screen_x, cfg.screen_y, cfg.screen_w, cfg.screen_h);
            printf("Offset (%d,%d) Area %d x %d rotated %d degrees\n",
                cfg.pos_x, cfg.pos_y, cfg.size_x, cfg.size_y, cfg.rot * 90);
        } else {
            printf("Warning: Config file format incorrect. Using defaults.\n");
        }
        fclose(f);
    } else {
        printf("Warning: config.txt not found. Using defaults.\n");
    }
    return cfg;
}

void MoveMouse(uint16_t raw_x, uint16_t raw_y, int click, TabletConfig cfg) {
    long rot_x, rot_y;

    // Rotation Logic
    switch (cfg.rot % 4) {
        case 0: // 0 deg
            rot_x = raw_x;
            rot_y = raw_y;
            break;
        case 1: // 90 deg CW
            rot_x = raw_y;
            rot_y = MAX_TABLET_X - raw_x;
            break;
        case 2: // 180 deg CW
            rot_x = MAX_TABLET_X - raw_x;
            rot_y = MAX_TABLET_Y - raw_y;
            break;
        case 3: // 270 deg CW
            rot_x = MAX_TABLET_Y - raw_y;
            rot_y = raw_x;
            break;
        default:
            rot_x = raw_x;
            rot_y = raw_y;
    }

    // Apply Tablet Offset & Cropping
    long adjusted_x = rot_x - cfg.pos_x;
    long adjusted_y = rot_y - cfg.pos_y;

    // Clamp to defined tablet area
    if (adjusted_x < 0) adjusted_x = 0;
    if (adjusted_y < 0) adjusted_y = 0;
    if (adjusted_x > cfg.size_x) adjusted_x = cfg.size_x;
    if (adjusted_y > cfg.size_y) adjusted_y = cfg.size_y;

    // Normalized position (0.0 to 1.0) within the tablet area
    float norm_x = (float)adjusted_x / (float)cfg.size_x;
    float norm_y = (float)adjusted_y / (float)cfg.size_y;

    // Map to pixel coordinate on the target monitor
    // In X11, screen coordinates are absolute across the whole virtual desktop
    int target_x = cfg.screen_x + (int)(norm_x * cfg.screen_w);
    int target_y = cfg.screen_y + (int)(norm_y * cfg.screen_h);

    // Move the pointer to the absolute screen position
    Window root = DefaultRootWindow(g_display);
    XWarpPointer(g_display, None, root, 0, 0, 0, 0, target_x, target_y);

    // Handle left button click state changes via XTest
    static int last_click = 0;
    if (click != last_click) {
        // Button 1 = left mouse button; True = press, False = release
        XTestFakeButtonEvent(g_display, 1, click ? True : False, CurrentTime);
        last_click = click;
    }

    XFlush(g_display);
}

int main() {
    // Open X11 display
    g_display = XOpenDisplay(NULL);
    if (!g_display) {
        printf("Failed to open X11 display. Is DISPLAY set?\n");
        return 1;
    }

    // Verify XTest extension is available
    int xtest_event_base, xtest_error_base, xtest_major, xtest_minor;
    if (!XTestQueryExtension(g_display, &xtest_event_base, &xtest_error_base,
                             &xtest_major, &xtest_minor)) {
        printf("XTest extension not available on this display.\n");
        XCloseDisplay(g_display);
        return 1;
    }

    if (hid_init() != 0) {
        printf("Failed to initialize HIDAPI\n");
        XCloseDisplay(g_display);
        return 1;
    }

    TabletConfig config = load_config();
    hid_device *handle = NULL;
    struct hid_device_info *devs, *cur_dev;

    printf("Searching for Wacom device...\n");

    // VID (1386 / 0x056A) | PID (890 / 0x037A)
    devs = hid_enumerate(0x056A, 0x037A);
    cur_dev = devs;

    while (cur_dev) {
        // CTL-472 specific usage page for raw position data
        if (cur_dev->usage_page == 0xff0d) {
            handle = hid_open_path(cur_dev->path);
            if (handle) {
                printf("Device found and opened.\n");
                break;
            }
        }
        cur_dev = cur_dev->next;
    }
    hid_free_enumeration(devs);

    if (!handle) {
        printf("Device not found or could not be opened.\n");
        printf("Try running as root or add a udev rule for the tablet.\n");
        XCloseDisplay(g_display);
        return 1;
    }

    // Send initialization feature report (CTL-472 specific)
    unsigned char init_msg[] = { 0x02, 0x02, 0x00 };
    hid_send_feature_report(handle, init_msg, sizeof(init_msg));

    unsigned char buf[11]; // CTL-472 specific buffer size
    printf("Driver running. Press Ctrl+C to exit.\n");

    while (1) {
        int res = hid_read(handle, buf, sizeof(buf));
        if (res < 0) {
            printf("Error reading device.\n");
            break;
        }

        // Parse Wacom Report
        if (res >= 8 && buf[0] == 0x02) {
            uint8_t status = buf[1];
            int proximity = (status & 0x80);

            if (proximity) {
                uint16_t x = buf[2] | (buf[3] << 8);
                uint16_t y = buf[4] | (buf[5] << 8);
                int tip_pressed = (status & 0x01);

                // Filter out 0,0 glitch
                if (x > 0 || y > 0) {
                    MoveMouse(x, y, tip_pressed, config);
                }
            }
        }
    }

    hid_close(handle);
    hid_exit();
    XCloseDisplay(g_display);
    return 0;
}
