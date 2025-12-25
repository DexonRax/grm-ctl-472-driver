#include <stdio.h>
#include <stdint.h>
#include <hidapi.h>
#include <windows.h>

typedef struct {
    int max_x;
    int max_y;
} TabletConfig;


TabletConfig load_config() {
    TabletConfig cfg = {15200, 9500}; // default values
    FILE *f = fopen("config.txt", "r");
    if (f) {
        if (fscanf(f, "MAX_X=%d\nMAX_Y=%d", &cfg.max_x, &cfg.max_y) == 2) {
            printf("Config loaded: Area %d x %d\n", cfg.max_x, cfg.max_y);
        }
        fclose(f);
    } else {
        printf("config.txt not found, using default config.\n");
    }
    return cfg;
}

void MoveMouse(uint16_t x, uint16_t y, int click, TabletConfig cfg) {
    if (x > cfg.max_x) x = cfg.max_x;
    if (y > cfg.max_y) y = cfg.max_y;

    INPUT input = {0};
    input.type = INPUT_MOUSE;

    // Scalling for Windows (0-65535)
    input.mi.dx = (long)((float)x * (65535.0f / (float)cfg.max_x));
    input.mi.dy = (long)((float)y * (65535.0f / (float)cfg.max_y));

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
    printf("Driver running. Area: 0..%d, 0..%d\n", config.max_x, config.max_y);

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