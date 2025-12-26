#include <stdio.h>
#include <stdint.h>
#include <hidapi.h>
#include <windows.h>

typedef struct {
    int pos_x;
    int pos_y;
    int size_x;
    int size_y;
    int rot;
} TabletConfig;

    
#define MAX_TABLET_X 15200
#define MAX_TABLET_Y 9500


TabletConfig load_config() {
    // Default values if file is missing or corrupt
    TabletConfig cfg = {0, 0, 15200, 9500, 0}; 
    FILE *f = fopen("config.txt", "r");
    if (f) {
        if (fscanf(f, "POS_X=%d\nPOS_Y=%d\nSIZE_X=%d\nSIZE_Y=%d\nROT=%d", 
            &cfg.pos_x, &cfg.pos_y, &cfg.size_x, &cfg.size_y, &cfg.rot) == 5) {
            printf("Config loaded: Offset (%d,%d) Area %d x %d rotated %d degrees\n", 
                cfg.pos_x, cfg.pos_y, cfg.size_x, cfg.size_y, cfg.rot*90);
        }
        fclose(f);
    }
    return cfg;
}

void MoveMouse(uint16_t raw_x, uint16_t raw_y, int click, TabletConfig cfg) {
    long rot_x, rot_y;

    // Rotation Logic
    // Maps raw physical coordinates to logical coordinates based on rotation anchor
    switch (cfg.rot % 4) {
        case 0: // 0 deg - Anchor Top-Left (Standard)
            rot_x = raw_x;
            rot_y = raw_y;
            break;
        case 1: // 90 deg CW - Anchor Top-Right
            rot_x = raw_y;
            rot_y = MAX_TABLET_X - raw_x;
            break;
        case 2: // 180 deg CW - Anchor Bottom-Right
            rot_x = MAX_TABLET_X - raw_x;
            rot_y = MAX_TABLET_Y - raw_y;
            break;
        case 3: // 270 deg CW - Anchor Bottom-Left
            rot_x = MAX_TABLET_Y - raw_y;
            rot_y = raw_x;
            break;
        default:
            rot_x = raw_x;
            rot_y = raw_y;
    }

    // Apply Offset (pos_x / pos_y) using the rotated coordinates
    long adjusted_x = rot_x - cfg.pos_x;
    long adjusted_y = rot_y - cfg.pos_y;

    // Clamp to the defined area size
    if (adjusted_x < 0) adjusted_x = 0;
    if (adjusted_y < 0) adjusted_y = 0;
    if (adjusted_x > cfg.size_x) adjusted_x = cfg.size_x;
    if (adjusted_y > cfg.size_y) adjusted_y = cfg.size_y;

    INPUT input = {0};
    input.type = INPUT_MOUSE;

    // Scaling for Windows (0-65535)
    input.mi.dx = (long)((float)adjusted_x * (65535.0f / (float)cfg.size_x));
    input.mi.dy = (long)((float)adjusted_y * (65535.0f / (float)cfg.size_y));

    input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE | MOUSEEVENTF_VIRTUALDESK;

    static int last_click = 0;
    if (click != last_click) {
        input.mi.dwFlags |= (click ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP);
        last_click = click;
    }

    SendInput(1, &input, sizeof(INPUT));
}



int main() {
    if (hid_init() != 0) return 1;

    TabletConfig config = load_config();
    hid_device *handle = NULL;
    struct hid_device_info *devs, *cur_dev;

    devs = hid_enumerate(0x056A, 0x037A); // VID (1386) / PID (890)
    cur_dev = devs;
    while (cur_dev) {
        if (cur_dev->usage_page == 0xff0d) { // Wacom specific usage page for raw position data
            handle = hid_open_path(cur_dev->path);
            break;
        }
        cur_dev = cur_dev->next;
    }
    hid_free_enumeration(devs);

    if (!handle) return 1;

    unsigned char init_msg[] = { 0x02, 0x02, 0x00 };
    hid_send_feature_report(handle, init_msg, sizeof(init_msg));

    unsigned char buf[10];
    printf("Driver running.");

    while (1) {
        int res = hid_read(handle, buf, sizeof(buf));
        if (res < 0) break;

        if (res >= 8 && buf[0] == 0x02) {
            uint8_t status = buf[1];
            int proximity = (status & 0x80); 
            
            if (proximity) {
                uint16_t x = buf[2] | (buf[3] << 8);
                uint16_t y = buf[4] | (buf[5] << 8);
                int tip_pressed = (status & 0x01);

                // Ignore x:0, y:0 to prevent cursor from jumping to top left corner 
                if (x > 0 || y > 0) {
                    MoveMouse(x, y, tip_pressed, config);
                }
            }
        }
    }

    hid_close(handle);
    hid_exit();
    return 0;
}