#include <stdio.h>
#include <stdint.h>
#include <hidapi.h>
#include <windows.h>

typedef struct {
    int pos_x;
    int pos_y;
    int size_x;
    int size_y;
} TabletConfig;


TabletConfig load_config() {
    // Default values if file is missing or corrupt
    TabletConfig cfg = {0, 0, 15200, 9500}; 
    FILE *f = fopen("config.txt", "r");
    if (f) {
        if (fscanf(f, "POS_X=%d\nPOS_Y=%d\nSIZE_X=%d\nSIZE_Y=%d", 
            &cfg.pos_x, &cfg.pos_y, &cfg.size_x, &cfg.size_y) == 4) {
            printf("Config loaded: Offset (%d,%d) Area %d x %d\n", 
                cfg.pos_x, cfg.pos_y, cfg.size_x, cfg.size_y);
        }
        fclose(f);
    }
    return cfg;
}

void MoveMouse(uint16_t x, uint16_t y, int click, TabletConfig cfg) {
    // 1. Apply Offset (pos_x / pos_y)
    // We use long to prevent underflow if the raw x is smaller than the offset
    long adjusted_x = (long)x - cfg.pos_x;
    long adjusted_y = (long)y - cfg.pos_y;

    // 2. Clamp to the defined area size
    if (adjusted_x < 0) adjusted_x = 0;
    if (adjusted_y < 0) adjusted_y = 0;
    if (adjusted_x > cfg.size_x) adjusted_x = cfg.size_x;
    if (adjusted_y > cfg.size_y) adjusted_y = cfg.size_y;

    INPUT input = {0};
    input.type = INPUT_MOUSE;

    // 3. Scaling for Windows (0-65535)
    // Formula: (Current_Pos / Max_Range) * 65535
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