/* dslink.c
 * Ben Wesch 2024
 * based on hidraw by Lucas Cordiviola <lucarda27@hotmail.com> 2022
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "m_pd.h"
#include <hidapi.h>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <unistd.h>
#endif

#if defined(__APPLE__)
#include <hidapi_darwin.h>
#endif

#if defined(_WIN32)
#include <hidapi_winapi.h>
#endif

#define DUALSENSE_VID 0x054C
#define DUALSENSE_PID 0x0CE6

#define INPUT_REPORT_USB_SIZE 64
#define INPUT_REPORT_BT_SIZE 78
#define INPUT_REPORT_BT_SHORT_SIZE 10

#define OUTPUT_REPORT_SIZE 47 // common size without report id
#define OUTPUT_REPORT_USB_SIZE 48
#define OUTPUT_REPORT_BT_SIZE 78
#define OUTPUT_REPORT_BT_CHECK_SIZE 75 // without checksum, but prepended salt

#define USB_REPORT_ID 0x02
#define BT_REPORT_ID 0x31

#define CALIBRATION_REPORT_SIZE 41
#define CALIBRATION_FEATURE_REPORT_ID 0x05

#define OFFSET_MOTOR_RIGHT 2
#define OFFSET_MOTOR_LEFT 3
#define OFFSET_LED_R 44
#define OFFSET_LED_G 45
#define OFFSET_LED_B 46
#define OFFSET_PLAYER_LEDS 43
#define OFFSET_MUTE_LED 8
#define OFFSET_LEFT_TRIGGER 9
#define OFFSET_RIGHT_TRIGGER 20

typedef struct _dslink {
    t_object x_obj;
    hid_device *handle;
    unsigned char input_buf[INPUT_REPORT_BT_SIZE];
    unsigned char output_buf[OUTPUT_REPORT_SIZE];
    int is_bluetooth;
    t_outlet *data_out;
    // t_outlet *debug_outlet;
    t_clock *poll_clock;
    t_float poll_interval;
    unsigned char sequence_number;
} t_dslink;

t_class *dslink_class;

// utility function prototypes
static uint32_t crc32(const uint8_t *data, size_t len);
static void parse_input_report(t_dslink *x, const unsigned char *buf);
static void output_value_message(t_outlet *outlet, const char *parts[], int num_parts, t_float value);
static void output_symbol_message(t_outlet *outlet, const char *parts[], int num_parts, const char *value);


static void dslink_write(t_dslink *x) {
    if (!x->handle) {
        pd_error(x, "dslink: no device opened");
        return;
    }

    // t_atom output_atoms[OUTPUT_REPORT_BT_SIZE]; // for debug purposes
    // int num_bytes;

    if (x->is_bluetooth) {
        unsigned char bt_buffer[OUTPUT_REPORT_BT_SIZE];
        unsigned char crc_buffer[OUTPUT_REPORT_BT_CHECK_SIZE];

        // Prepare CRC buffer
        crc_buffer[0] = 0xA2;  // Salt
        crc_buffer[1] = BT_REPORT_ID;
        crc_buffer[2] = x->sequence_number;
        crc_buffer[3] = 0x10;  // Static value

        // Copy the common output buffer
        memcpy(crc_buffer + 4, x->output_buf, OUTPUT_REPORT_SIZE);

        // Zero-fill the rest of the CRC buffer
        memset(crc_buffer + 4 + OUTPUT_REPORT_SIZE, 0, OUTPUT_REPORT_BT_CHECK_SIZE - (4 + OUTPUT_REPORT_SIZE));

        // Calculate CRC
        uint32_t crc = crc32(crc_buffer, OUTPUT_REPORT_BT_CHECK_SIZE);

        // Prepare the actual Bluetooth output buffer
        memcpy(bt_buffer, crc_buffer + 1, OUTPUT_REPORT_BT_SIZE - 4);  // Copy everything except the salt and CRC
        bt_buffer[74] = (crc >> 0) & 0xFF;
        bt_buffer[75] = (crc >> 8) & 0xFF;
        bt_buffer[76] = (crc >> 16) & 0xFF;
        bt_buffer[77] = (crc >> 24) & 0xFF;

        // Increment sequence number
        x->sequence_number = (x->sequence_number + 16) & 0xFF;

        // // Prepare output atoms
        // for (int i = 0; i < OUTPUT_REPORT_BT_SIZE; i++) {
        //     SETFLOAT(&output_atoms[i], (t_float)bt_buffer[i]);
        // }
        // num_bytes = OUTPUT_REPORT_BT_SIZE;

        // Write the Bluetooth report
        int res = hid_write(x->handle, bt_buffer, OUTPUT_REPORT_BT_SIZE);
        if (res < 0) {
            pd_error(x, "dslink: error writing to device (Bluetooth mode)");
        }
    } else {
        // USB mode
        unsigned char usb_buffer[OUTPUT_REPORT_USB_SIZE];
        usb_buffer[0] = USB_REPORT_ID;
        memcpy(usb_buffer + 1, x->output_buf, OUTPUT_REPORT_SIZE);

        // // Prepare output atoms
        // for (int i = 0; i < OUTPUT_REPORT_USB_SIZE; i++) {
        //     SETFLOAT(&output_atoms[i], (t_float)usb_buffer[i]);
        // }
        // num_bytes = OUTPUT_REPORT_USB_SIZE;

        // Write the USB report
        int res = hid_write(x->handle, usb_buffer, OUTPUT_REPORT_USB_SIZE);
        if (res < 0) {
            pd_error(x, "dslink: error writing to device (USB mode)");
        }
    }
    // outlet_list(x->debug_outlet, &s_list, num_bytes, output_atoms);
}

static void dslink_open(t_dslink *x) {
    if (x->handle) {
        hid_close(x->handle);
    }

    x->handle = hid_open(DUALSENSE_VID, DUALSENSE_PID, NULL);
    if (!x->handle) {
        pd_error(x, "dslink: unable to open device");
        return;
    }

    // Request calibration report
    unsigned char calibration_buf[CALIBRATION_REPORT_SIZE] = {0};
    calibration_buf[0] = CALIBRATION_FEATURE_REPORT_ID;
    int res = hid_get_feature_report(x->handle, calibration_buf, sizeof(calibration_buf));
    
    if (res < 0) {
        pd_error(x, "dslink: failed to get calibration report");
        // Continue anyway, as this might still work for USB connections
    } else {
        post("dslink: received calibration report, size: %d", res);
        // Here we could parse the calibration data if needed
    }

    // Detect if we're in Bluetooth or USB mode
    unsigned char buf[256];
    res = hid_read_timeout(x->handle, buf, sizeof(buf), 1000);

    if (res == INPUT_REPORT_BT_SIZE || res == INPUT_REPORT_BT_SHORT_SIZE) {
        x->is_bluetooth = 1;
        post("dslink: connected via Bluetooth");
    } else if (res == INPUT_REPORT_USB_SIZE) {
        x->is_bluetooth = 0;
        post("dslink: connected via USB");
    } else {
        pd_error(x, "dslink: unable to determine connection type");
        hid_close(x->handle);
        x->handle = NULL;
        return;
    }

    // Initialize the output report buffer
    memset(x->output_buf, 0, OUTPUT_REPORT_SIZE);
    
    // Set some default values
    x->output_buf[0] = 0xFF;
    x->output_buf[1] = 0x7F;
    x->output_buf[5] = 0xFF;
    x->output_buf[OFFSET_LED_R] = 0; // Red
    x->output_buf[OFFSET_LED_G] = 0; // Green
    x->output_buf[OFFSET_LED_B] = 0; // Blue
    x->output_buf[OFFSET_PLAYER_LEDS] = 0x00;
    x->output_buf[OFFSET_MUTE_LED] = 0x00; // Mute LED off
    dslink_write(x);
    x->output_buf[1] = 0x77; // release LED

    hid_set_nonblocking(x->handle, 1);
}

static void dslink_close(t_dslink *x) {
    if (x->handle) {
        hid_close(x->handle);
        x->handle = NULL;
    }
}

static void dslink_poll(t_dslink *x, t_floatarg f) {
    x->poll_interval = f;
    if (f > 0) clock_delay(x->poll_clock, 0);
    else clock_unset(x->poll_clock);
}

static inline int dslink_read(t_dslink *x) {
    if (!x->handle) {
        pd_error(x, "dslink: no device opened");
        return 0;
    }

    int res = hid_read(x->handle, x->input_buf, INPUT_REPORT_BT_SIZE);
    if (res > 0) {
        parse_input_report(x, x->input_buf);
    } else if (res < 0) {
        pd_error(x, "dslink: error reading from device");
    }

    if (x->poll_interval > 0) {
        clock_delay(x->poll_clock, x->poll_interval);
    }
    return 1;
}

static void dslink_tick(t_dslink *x)
{
    if (dslink_read(x))
        clock_delay(x->poll_clock, x->poll_interval);
}

static void dslink_set_motor(t_dslink *x, t_symbol *s, t_floatarg value) {
    if (!x->handle) {
        pd_error(x, "dslink: no device opened");
        return;
    }

    post("name: %s", s->s_name);
    int offset = (s == gensym("right")) ? OFFSET_MOTOR_RIGHT : OFFSET_MOTOR_LEFT;

    // Set motor intensity
    x->output_buf[offset] = (unsigned char)(value * 255);

    // Write the updated report
    dslink_write(x);
}

static void dslink_set_led(t_dslink *x, t_symbol *s, int argc, t_atom *argv) {
    (void)s;

    if (!x->handle) {
        pd_error(x, "dslink: no device opened");
        return;
    }

    unsigned char state;
    t_symbol *type = atom_getsymbolarg(0, argc, argv);

    if (type == gensym("mute"))
    {
        state = argc > 1 ? atom_getintarg(1, argc, argv) : 0;
        x->output_buf[OFFSET_MUTE_LED] = state; // mask?
    }
    else if (type == gensym("players"))
    {
        state = argc > 1 ? atom_getintarg(1, argc, argv) : 0;
        x->output_buf[OFFSET_PLAYER_LEDS] = state & 0x1F;  // player LEDs Only use the lower 5 bits
    }
    else if (type == gensym("color"))
    {
        if (argc != 4)
            pd_error(x, "dslink: wrong argument count for color");
        x->output_buf[OFFSET_LED_R] = atom_getintarg(1, argc, argv);
        x->output_buf[OFFSET_LED_G] = atom_getintarg(2, argc, argv);
        x->output_buf[OFFSET_LED_B] = atom_getintarg(3, argc, argv);
    }
    dslink_write(x);
}

static void dslink_set_trigger(t_dslink *x, t_symbol *s, int argc, t_atom *argv) {
    if (!x->handle || argc > 11) {
        pd_error(x, "dslink: no device opened or invalid trigger settings");
        return;
    }

    int offset = (s == gensym("left")) ? OFFSET_LEFT_TRIGGER : OFFSET_RIGHT_TRIGGER;

    for (int i = 0; i < 11; i++) {
        x->output_buf[offset + i] = (unsigned char)atom_getfloat(&argv[i]);
    }

    dslink_write(x);
}

static void dslink_free(t_dslink *x) {
    dslink_close(x);
    clock_free(x->poll_clock);
}

static void *dslink_new(void) {
    t_dslink *x = (t_dslink *)pd_new(dslink_class);

    x->data_out = outlet_new(&x->x_obj, &s_anything);
    // x->debug_outlet = outlet_new(&x->x_obj, &s_list);

    x->poll_clock = clock_new(x, (t_method)dslink_tick);
    x->poll_interval = 0;
    x->handle = NULL;
    x->sequence_number = 0;

    return (void *)x;
}

// Utility functions

static void output_value_message(t_outlet *outlet, const char *parts[], int num_parts, t_float value) {
    t_atom atoms[num_parts + 1];
    for (int i = 0; i < num_parts; i++) {
        SETSYMBOL(&atoms[i], gensym(parts[i]));
    }
    SETFLOAT(&atoms[num_parts], value);
    outlet_anything(outlet, gensym(parts[0]), num_parts, &atoms[1]);
}

static void output_symbol_message(t_outlet *outlet, const char *parts[], int num_parts, const char *value) {
    t_atom atoms[num_parts + 1];
    for (int i = 0; i < num_parts; i++) {
        SETSYMBOL(&atoms[i], gensym(parts[i]));
    }
    SETSYMBOL(&atoms[num_parts], gensym(value));
    outlet_anything(outlet, gensym(parts[0]), num_parts, &atoms[1]);
}

static uint32_t crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

static void parse_input_report(t_dslink *x, const unsigned char *buf) {
    int offset = x->is_bluetooth ? 2 : 1;

    // Left stick
    output_value_message(x->data_out, (const char*[]){"stick", "l", "x"}, 3, (buf[offset + 0] - 128) / 128.0f);
    output_value_message(x->data_out, (const char*[]){"stick", "l", "y"}, 3, (buf[offset + 1] - 128) / -128.0f);

    // Right stick
    output_value_message(x->data_out, (const char*[]){"stick", "r", "x"}, 3, (buf[offset + 2] - 128) / 128.0f);
    output_value_message(x->data_out, (const char*[]){"stick", "r", "y"}, 3, (buf[offset + 3] - 128) / -128.0f);

    // Triggers
    output_value_message(x->data_out, (const char*[]){"trigger", "l"}, 2, buf[offset + 4] / 255.0f);
    output_value_message(x->data_out, (const char*[]){"trigger", "r"}, 2, buf[offset + 5] / 255.0f);

    // Buttons
    output_value_message(x->data_out, (const char*[]){"button", "triangle"}, 2, (buf[offset + 7] & 0x80) != 0);
    output_value_message(x->data_out, (const char*[]){"button", "circle"}, 2, (buf[offset + 7] & 0x40) != 0);
    output_value_message(x->data_out, (const char*[]){"button", "cross"}, 2, (buf[offset + 7] & 0x20) != 0);
    output_value_message(x->data_out, (const char*[]){"button", "square"}, 2, (buf[offset + 7] & 0x10) != 0);
    output_value_message(x->data_out, (const char*[]){"button", "l1"}, 2, (buf[offset + 8] & 0x01) != 0);
    output_value_message(x->data_out, (const char*[]){"button", "r1"}, 2, (buf[offset + 8] & 0x02) != 0);
    output_value_message(x->data_out, (const char*[]){"button", "l2"}, 2, (buf[offset + 8] & 0x04) != 0);
    output_value_message(x->data_out, (const char*[]){"button", "r2"}, 2, (buf[offset + 8] & 0x08) != 0);
    output_value_message(x->data_out, (const char*[]){"button", "create"}, 2, (buf[offset + 8] & 0x10) != 0);
    output_value_message(x->data_out, (const char*[]){"button", "options"}, 2, (buf[offset + 8] & 0x20) != 0);
    output_value_message(x->data_out, (const char*[]){"button", "l3"}, 2, (buf[offset + 8] & 0x40) != 0);
    output_value_message(x->data_out, (const char*[]){"button", "r3"}, 2, (buf[offset + 8] & 0x80) != 0);
    output_value_message(x->data_out, (const char*[]){"button", "ps"}, 2, (buf[offset + 9] & 0x01) != 0);
    output_value_message(x->data_out, (const char*[]){"button", "pad"}, 2, (buf[offset + 9] & 0x02) != 0);
    output_value_message(x->data_out, (const char*[]){"button", "mute"}, 2, (buf[offset + 9] & 0x04) != 0);

    // D-pad
    uint8_t dpad = buf[offset + 7] & 0x0F;
    int dpad_x = 0, dpad_y = 0;
    switch (dpad) {
        case 0: dpad_y = 1; break;
        case 1: dpad_x = 1; dpad_y = 1; break;
        case 2: dpad_x = 1; break;
        case 3: dpad_x = 1; dpad_y = -1; break;
        case 4: dpad_y = -1; break;
        case 5: dpad_x = -1; dpad_y = -1; break;
        case 6: dpad_x = -1; break;
        case 7: dpad_x = -1; dpad_y = 1; break;
    }
    output_value_message(x->data_out, (const char*[]){"dpad", "x"}, 2, dpad_x);
    output_value_message(x->data_out, (const char*[]){"dpad", "y"}, 2, dpad_y);

    // Gyroscope
    uint16_t gyro_x_raw = (uint16_t)((buf[offset + 17]) | buf[offset + 16] << 8);
    uint16_t gyro_y_raw = (uint16_t)((buf[offset + 19]) | buf[offset + 18] << 8);
    uint16_t gyro_z_raw = (uint16_t)((buf[offset + 21]) | buf[offset + 20] << 8);

    t_float gyro_x = (t_float)((gyro_x_raw > 32767) ? gyro_x_raw - 65536 : gyro_x_raw) / 8192.0f;
    t_float gyro_y = (t_float)((gyro_y_raw > 32767) ? gyro_y_raw - 65536 : gyro_y_raw) / 8192.0f;
    t_float gyro_z = (t_float)((gyro_z_raw > 32767) ? gyro_z_raw - 65536 : gyro_z_raw) / 8192.0f;

    output_value_message(x->data_out, (const char*[]){"gyro", "x"}, 2, gyro_x);
    output_value_message(x->data_out, (const char*[]){"gyro", "y"}, 2, gyro_y);
    output_value_message(x->data_out, (const char*[]){"gyro", "z"}, 2, gyro_z);

    // Accelerometer
    uint16_t accel_x_raw = (uint16_t)((buf[offset + 23]) | buf[offset + 22] << 8);
    uint16_t accel_y_raw = (uint16_t)((buf[offset + 25]) | buf[offset + 24] << 8);
    uint16_t accel_z_raw = (uint16_t)((buf[offset + 27]) | buf[offset + 26] << 8);

    t_float accel_x = (t_float)((accel_x_raw > 32767) ? accel_x_raw - 65536 : accel_x_raw) / 8192.0f;
    t_float accel_y = (t_float)((accel_y_raw > 32767) ? accel_y_raw - 65536 : accel_y_raw) / 8192.0f;
    t_float accel_z = (t_float)((accel_z_raw > 32767) ? accel_z_raw - 65536 : accel_z_raw) / 8192.0f;

    output_value_message(x->data_out, (const char*[]){"accel", "x"}, 2, accel_x);
    output_value_message(x->data_out, (const char*[]){"accel", "y"}, 2, accel_y);
    output_value_message(x->data_out, (const char*[]){"accel", "z"}, 2, accel_z);

    // Touchpad
    for (int i = 0; i < 2; i++) {
        int touch_offset = offset + 32 + (i * 4);
        uint8_t touch_data1 = buf[touch_offset];
        uint8_t touch_data2 = buf[touch_offset + 1];
        uint8_t touch_data3 = buf[touch_offset + 2];
        uint8_t touch_data4 = buf[touch_offset + 3];
        int is_active = !(touch_data1 & 0x80);
        
        const char* touch_id = (i == 0) ? "touch_0" : "touch_1";
        
        if (is_active) {
            int touch_x = ((touch_data3 & 0x0F) << 8) | touch_data2;
            int touch_y = (touch_data4 << 4) | ((touch_data3 & 0xF0) >> 4);
            
            output_value_message(x->data_out, (const char*[]){"pad", touch_id, "active"}, 3, 1.0f);
            output_value_message(x->data_out, (const char*[]){"pad", touch_id, "x"}, 3, touch_x / 1920.0f);
            output_value_message(x->data_out, (const char*[]){"pad", touch_id, "y"}, 3, touch_y / 1080.0f);
        } else {
            output_value_message(x->data_out, (const char*[]){"pad", touch_id, "active"}, 3, 0.0f);
        }
    }

    // Battery level and status
    uint8_t battery_data = buf[offset + 52];
    int battery_level = battery_data & 0x0F;
    int battery_status = (battery_data & 0xF0) >> 4;

    output_value_message(x->data_out, (const char*[]){"battery", "level"}, 2, battery_level);

    const char *status_str;
    switch (battery_status) {
        case 0x0: status_str = "discharging"; break;
        case 0x1: status_str = "charging"; break;
        case 0x2: status_str = "full"; break;
        case 0xA: status_str = "temp_high"; break;
        case 0xB: status_str = "temp_low"; break;
        default: status_str = "unknown";
    }
    output_symbol_message(x->data_out, (const char*[]){"battery", "status"}, 2, status_str);

    // Connection type (already known from device opening)
    output_symbol_message(x->data_out, (const char*[]){"connection"}, 1, x->is_bluetooth ? "bluetooth" : "usb");

    // Headphone and mic status
    uint8_t headset_data = buf[offset + 53];
    output_value_message(x->data_out, (const char*[]){"headphones"}, 1, (headset_data & 0x01) != 0);
    output_value_message(x->data_out, (const char*[]){"microphone"}, 1, (headset_data & 0x02) != 0);

    // Haptic feedback motors active status
    output_value_message(x->data_out, (const char*[]){"haptic", "active"}, 2, (buf[offset + 54] & 0x02) != 0);
}


#if defined(_WIN32)
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
void dslink_setup(void) {
    dslink_class = class_new(gensym("dslink"),
                                (t_newmethod)dslink_new,
                                (t_method)dslink_free,
                                sizeof(t_dslink),
                                CLASS_DEFAULT,
                                0);

    class_addbang(dslink_class, dslink_read);
    class_addmethod(dslink_class, (t_method)dslink_read, gensym("read"), 0);
    class_addmethod(dslink_class, (t_method)dslink_open, gensym("open"), 0);
    class_addmethod(dslink_class, (t_method)dslink_close, gensym("close"), 0);
    class_addmethod(dslink_class, (t_method)dslink_poll, gensym("poll"), A_FLOAT, 0);
    class_addmethod(dslink_class, (t_method)dslink_set_motor, gensym("motor"), A_SYMBOL, A_FLOAT, 0);
    class_addmethod(dslink_class, (t_method)dslink_set_led, gensym("led"), A_GIMME, 0);
    class_addmethod(dslink_class, (t_method)dslink_set_trigger, gensym("trigger"), A_GIMME, 0);

    post("dslink external for Pure Data");
    post("compatible with Sony DualSense controller");

    hid_init();

#if defined(__APPLE__)
    // To work properly needs to be called before hid_open/hid_open_path after hid_init.
    // Best/recommended option - call it right after hid_init.
    hid_darwin_set_open_exclusive(0);
#endif
}
