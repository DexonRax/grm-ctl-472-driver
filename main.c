#include <stdio.h>
#include <stdint.h>
#include <hidapi.h>
#include <windows.h>

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

int v_top    = -1;
int v_left   = -1;
int v_width  = -1;
int v_height = -1;

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
                cfg.pos_x, cfg.pos_y, cfg.size_x, cfg.size_y, cfg.rot*90);
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

    // Clamp values (prevent going out of defined tablet area)
    if (adjusted_x < 0) adjusted_x = 0;
    if (adjusted_y < 0) adjusted_y = 0;
    if (adjusted_x > cfg.size_x) adjusted_x = cfg.size_x;
    if (adjusted_y > cfg.size_y) adjusted_y = cfg.size_y;

    // Calculate normalized position (0.0 to 1.0) within the tablet area
    float norm_x = (float)adjusted_x / (float)cfg.size_x;
    float norm_y = (float)adjusted_y / (float)cfg.size_y;

    // Calculate the exact pixel coordinate on the specific monitor
    long target_pixel_x = cfg.screen_x + (long)(norm_x * cfg.screen_w);
    long target_pixel_y = cfg.screen_y + (long)(norm_y * cfg.screen_h);


    // Convert pixel coordinate to absolute range (0..65535) relative to the virtual desktop
    long abs_x = (long)(((double)(target_pixel_x - v_left) / v_width) * 65535.0);
    long abs_y = (long)(((double)(target_pixel_y - v_top) / v_height) * 65535.0);

    // Send Input
    INPUT input = {0};
    input.type = INPUT_MOUSE;
    input.mi.dx = abs_x;
    input.mi.dy = abs_y;

    // MOUSEEVENTF_VIRTUALDESK for multi-monitor setups
    input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE | MOUSEEVENTF_VIRTUALDESK;

    static int last_click = 0;
    if (click != last_click) {
        input.mi.dwFlags |= (click ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP);
        last_click = click;
    }

    SendInput(1, &input, sizeof(INPUT));
}

int main() {
    if (hid_init() != 0) {
        printf("Failed to initialize HIDAPI\n");
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
        return 1;
    }

    // Send initialization feature report (CTL-472 specific)
    unsigned char init_msg[] = { 0x02, 0x02, 0x00 };
    hid_send_feature_report(handle, init_msg, sizeof(init_msg));

    unsigned char buf[11]; //CTL-472 specific buffer size
    printf("Driver running. Press Ctrl+C to exit.\n");

    v_top    = GetSystemMetrics(SM_YVIRTUALSCREEN);
    v_left   = GetSystemMetrics(SM_XVIRTUALSCREEN);
    v_width  = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    v_height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

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
    return 0;
}