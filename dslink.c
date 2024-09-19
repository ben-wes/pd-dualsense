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

#define DSLINK_MAJOR_VERSION 0
#define DSLINK_MINOR_VERSION 2
#define DSLINK_BUGFIX_VERSION 0

#define OPEN_POLL_INTERVAL 200

#define DUALSENSE_VID 0x054C
#define DUALSENSE_PID 0x0CE6

#define INPUT_REPORT_BT_SIZE 78
#define INPUT_REPORT_BT_SHORT_SIZE 10
#define INPUT_REPORT_USB_SIZE 64

#define OUTPUT_REPORT_BT_OFFSET 1 // skip salt
#define OUTPUT_REPORT_USB_OFFSET 3 // skip salt, bluetooth id and sequence byte (which isn't used)

#define OUTPUT_REPORT_USB_SIZE 48
#define OUTPUT_REPORT_BT_SIZE 78
#define OUTPUT_REPORT_BT_CHECK_SIZE 75 // without checksum, but prepended salt

#define BT_REPORT_SALT 0xA2 // prepended for crc32 checksum
#define BT_REPORT_ID 0x31
#define USB_REPORT_ID 0x02

#define REPORT_CONFIGURE1 0xFF
// source: https://github.com/nowrep/dualsensectl/blob/main/main.c
// COMPATIBLE_VIBRATION BIT(0) // <-- can somehow be compensated with motors bits below
// HAPTICS_SELECT BIT(1)
// RIGHT_TRIGGER_MOTOR_ENABLE BIT(2)
// LEFT_TRIGGER_MOTOR_ENABLE BIT(3)
// HEADPHONE_VOLUME_ENABLE BIT(4)
// SPEAKER_VOLUME_ENABLE BIT(5)
// MICROPHONE_VOLUME_ENABLE BIT(6)
// AUDIO_CONTROL_ENABLE BIT(7)

#define REPORT_CONFIGURE2 0xFF
#define REPORT_CONFIGURE2_LED_RELEASED 0xF7
// MIC_MUTE_LED_CONTROL_ENABLE BIT(0)
// POWER_SAVE_CONTROL_ENABLE BIT(1)
// LIGHTBAR_CONTROL_ENABLE BIT(2)
// RELEASE_LEDS BIT(3)
// PLAYER_INDICATOR_CONTROL_ENABLE BIT(4)
// BIT(5) ???
// VIBRATION_ATTENUATION_ENABLE BIT(6)
// AUDIO_CONTROL2_ENABLE BIT(7)

#define REPORT_CONFIGURE3 0xFF


#define CALIBRATION_REPORT_SIZE 41
#define CALIBRATION_FEATURE_REPORT_ID 0x05

// offsets for bluetooth report and prepended salt
#define OFFSET_MOTOR_RIGHT 6
#define OFFSET_MOTOR_LEFT 7
#define OFFSET_MUTE_LED 12
#define OFFSET_LEFT_TRIGGER 13
#define OFFSET_RIGHT_TRIGGER 24
#define OFFSET_CONFIGURE_LED_MOTORS 42 // higher bits seem to be relevant here, too, for motors
// LED_BRIGHTNESS_CONTROL_ENABLE BIT(0)
// LIGHTBAR_SETUP_CONTROL_ENABLE BIT(1)
// COMPATIBLE_VIBRATION2 BIT(2)
// ???

// #define OFFSET_CONFIGURE_LIGHTS 45 doesn't seem to do anything
#define OFFSET_PLAYER_LEDS_BRIGHTNESS 46 // expects just 1st bit to dim player leds
#define OFFSET_PLAYER_LEDS 47
#define OFFSET_LED_R 48
#define OFFSET_LED_G 49
#define OFFSET_LED_B 50

#define CRC32_POLYNOMIAL 0xEDB88320

typedef enum {
    BATTERY_UNKNOWN,
    BATTERY_DISCHARGING,
    BATTERY_CHARGING,
    BATTERY_FULL,
    BATTERY_TEMP_HIGH,
    BATTERY_TEMP_LOW
} battery_status_t;

typedef struct {
    struct {
        struct { t_float x, y; } l, r;
    } analog;
    struct { t_float l, r; } trigger;
    struct {
        t_float triangle, circle, cross, square;
        t_float l1, r1, l2, r2;
        t_float create, options, l3, r3;
        t_float ps, pad, mute;
    } button;
    struct { t_float x, y; } digital;
    struct { t_float x, y, z; } gyro, accel;
    struct {
        t_float active;
        t_float x, y;
    } touch1, touch2;
    t_float battery_level;
    t_float battery_status;
    t_float bluetooth;
    t_float headphones, microphone;
    t_float haptic_active;
} t_dslink_state;

typedef struct _dslink {
    t_object x_obj;
    hid_device *handle;
    unsigned char read_buf[INPUT_REPORT_BT_SIZE]; // init with max size
    unsigned char write_buf[OUTPUT_REPORT_BT_SIZE+1]; // max size + salt byte
    int write_size;
    int is_bluetooth;
    t_outlet *data_out;
    t_outlet *imu_out;
    t_outlet *status_out;
    t_clock *poll_clock;
    t_clock *open_clock;
    t_clock *write_clock; // clock for write scheduling
    t_float poll_interval;

    t_dslink_state state;
} t_dslink;

t_class *dslink_class;


// utility function prototypes
static uint32_t crc32_table[256];

static void generate_crc32_table();
static uint32_t crc32(const uint8_t *data, size_t len);
static void parse_input_report(t_dslink *x, const unsigned char *buf, int filter);
static void output_value(t_outlet *outlet, const char *parts[], int num_parts, t_float *state_value, t_float value, int filter);
static void do_write(t_dslink *x);
static int do_open(t_dslink *x);
static void poll_tick(t_dslink *x);


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

    int res = hid_read(x->handle, x->read_buf, INPUT_REPORT_BT_SIZE);
    if (res < 0) {
        pd_error(x, "dslink: error reading from device");
        return 0;
    }
    parse_input_report(x, x->read_buf, 1);

    if (x->poll_interval > 0) {
        clock_delay(x->poll_clock, x->poll_interval);
    }
    return 1;
}

static void dslink_open(t_dslink *x) {
    if (do_open(x))
        dslink_poll(x, 10);
    else
        pd_error(x, "dslink: unable to open device");
}

static void dslink_write(t_dslink *x) {
    // write if no other write is pending
    if (x->write_size != 0) return;

    x->write_size = x->is_bluetooth ? OUTPUT_REPORT_BT_SIZE : OUTPUT_REPORT_USB_SIZE;
    clock_delay(x->write_clock, 0);
}

static void dslink_close(t_dslink *x) {
    clock_unset(x->poll_clock);
    clock_unset(x->open_clock);
    if (x->handle) {
        hid_close(x->handle);
        x->handle = NULL;
        post("dslink: connection closed");
    }
}

static void dslink_set_motor(t_dslink *x, t_symbol *s, t_floatarg value) {
    if (!x->handle) {
        pd_error(x, "dslink: no device opened");
        return;
    }
    int offset = (s == gensym("right")) ? OFFSET_MOTOR_RIGHT : OFFSET_MOTOR_LEFT;
    x->write_buf[offset] = (unsigned char)(value * 255);
    dslink_write(x);
}

static void dslink_configure(t_dslink *x, t_float f) {
    x->write_buf[OFFSET_CONFIGURE_LED_MOTORS] = (unsigned char)f;
    // FIXME: should be exposed in more accessible way
    // check what other stuff should be done here
    // like attenuation etc.
}

static void dslink_set_led(t_dslink *x, t_symbol *s, int argc, t_atom *argv) {
    (void)s;

    if (!x->handle) {
        pd_error(x, "dslink: no device opened");
        return;
    }

    unsigned char value;
    t_symbol *type = atom_getsymbolarg(0, argc, argv);

    if (type == gensym("mute"))
    {
        value = atom_getintarg(1, argc, argv);
        x->write_buf[OFFSET_MUTE_LED] = value & 0xFF; // mask?
    }
    else if (type == gensym("brightness"))
    {
        value = atom_getintarg(1, argc, argv);
        x->write_buf[OFFSET_PLAYER_LEDS_BRIGHTNESS] = value > 0 ? 0 : 1;
    }
    else if (type == gensym("players"))
    {
        value = atom_getintarg(1, argc, argv);
        x->write_buf[OFFSET_PLAYER_LEDS] = value & 0x1F; // player LEDs only use the lower 5 bits
        // FIXME: could be done in a more elaborated way maybe - see https://github.com/nowrep/dualsensectl/blob/main/main.c#L656
    }
    else if (type == gensym("color"))
    {
        float r, g, b; // expecting 0..255
        float brightness; // expecting 0..1 - FIXME?

        r = atom_getfloatarg(1, argc, argv);
        g = argc > 2 ? atom_getfloatarg(2, argc, argv) : r;
        b = argc > 3 ? atom_getfloatarg(3, argc, argv) : r;
        brightness = argc > 4 ? atom_getfloatarg(4, argc, argv) : 1.0f;
        x->write_buf[OFFSET_LED_R] = (unsigned char)r * brightness;
        x->write_buf[OFFSET_LED_G] = (unsigned char)g * brightness;
        x->write_buf[OFFSET_LED_B] = (unsigned char)b * brightness;
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
        x->write_buf[offset + i] = (unsigned char)atom_getfloat(&argv[i]);
    }
    dslink_write(x);
}

static void dslink_state(t_dslink *x) {
    if (!x->handle) {
        pd_error(x, "dslink: no device opened");
        return;
    }

    dslink_read(x);
    parse_input_report(x, x->read_buf, 0);
}

// Utility functions

// perform actual HID write, called by clock
static void do_write(t_dslink *x) {
    if (!x->handle || x->write_size == 0) {
        pd_error(x, "dslink: no device opened or no data to write");
        return;
    }

    unsigned char *write_ptr;
    int write_size;

    if (x->is_bluetooth) {
        // append checksum bytes
        uint32_t crc = crc32(x->write_buf, OUTPUT_REPORT_BT_CHECK_SIZE);
        x->write_buf[75] = (crc >> 0) & 0xFF;
        x->write_buf[76] = (crc >> 8) & 0xFF;
        x->write_buf[77] = (crc >> 16) & 0xFF;
        x->write_buf[78] = (crc >> 24) & 0xFF;

        write_ptr = x->write_buf + OUTPUT_REPORT_BT_OFFSET;
        write_size = OUTPUT_REPORT_BT_SIZE;
    } else {
        write_ptr = x->write_buf + OUTPUT_REPORT_USB_OFFSET;
        write_size = OUTPUT_REPORT_USB_SIZE;
    }

    int res = hid_send_output_report(x->handle, write_ptr, write_size);
    if (res < 0) pd_error(x, "dslink: unable to write: %ls", hid_error(x->handle));

    x->write_size = 0;
}

static int do_open(t_dslink *x) {
    if (x->handle) hid_close(x->handle);

    x->handle = hid_open(DUALSENSE_VID, DUALSENSE_PID, NULL);
    if (!x->handle) return 0;

    // request calibration report
    unsigned char calibration_buf[CALIBRATION_REPORT_SIZE] = {0};
    calibration_buf[0] = CALIBRATION_FEATURE_REPORT_ID;
    int res = hid_get_feature_report(x->handle, calibration_buf, sizeof(calibration_buf));
    
    if (res < 0) pd_error(x, "dslink: failed to get calibration report");
    // continue anyway without calibration report, as this might still work for USB connections
    // for res > 0, we could parse the calibration data

    // detect if we're in Bluetooth or USB mode
    res = hid_read_timeout(x->handle, x->read_buf, INPUT_REPORT_BT_SIZE, 1000);

    memset(x->write_buf, 0, sizeof(x->write_buf));
    x->write_buf[0] = BT_REPORT_SALT;
    x->write_buf[1] = BT_REPORT_ID;

    if (res == INPUT_REPORT_BT_SIZE || res == INPUT_REPORT_BT_SHORT_SIZE) {
        x->is_bluetooth = 1;
        x->write_buf[3] = 0x10; // not sure necessity of 0x10 - copied from other dualsense integrations
        post("dslink: connected via Bluetooth");
    } else if (res == INPUT_REPORT_USB_SIZE) {
        x->is_bluetooth = 0;
        x->write_buf[3] = USB_REPORT_ID;
        post("dslink: connected via USB");
    } else
        pd_error(x, "dslink: unable to determine connection type");

    x->write_buf[4] = REPORT_CONFIGURE1;
    x->write_buf[5] = REPORT_CONFIGURE2;
    x->write_buf[9] = REPORT_CONFIGURE3;
    x->write_buf[OFFSET_CONFIGURE_LED_MOTORS] = 1; // allow LED brightness setting

    x->write_size = x->is_bluetooth ? OUTPUT_REPORT_BT_SIZE : OUTPUT_REPORT_USB_SIZE;
    do_write(x); // write immediately to ensure that LED flag will be switched
    x->write_buf[5] = REPORT_CONFIGURE2_LED_RELEASED; // release LED with next report

    hid_set_nonblocking(x->handle, 1);
    return 1;
}

static void poll_tick(t_dslink *x)
{
    if (dslink_read(x))
        clock_delay(x->poll_clock, x->poll_interval);
}

static void open_tick(t_dslink *x)
{
    if (do_open(x)) {
        clock_unset(x->open_clock);
        dslink_poll(x, 10);
    } else
        clock_delay(x->open_clock, OPEN_POLL_INTERVAL);
}

static void output_value(t_outlet *outlet, const char *parts[], int num_parts, t_float *state_value, t_float value, int filter) {
    if (*state_value != value || !filter) {
        *state_value = value;
        t_atom atoms[num_parts + 1];
        for (int i = 0; i < num_parts; i++) {
            SETSYMBOL(&atoms[i], gensym(parts[i]));
        }
        SETFLOAT(&atoms[num_parts], value);
        outlet_anything(outlet, gensym(parts[0]), num_parts, &atoms[1]);
    }
}

static void generate_crc32_table() {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
        crc32_table[i] = crc;
    }
}

static uint32_t crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc & 0xFF) ^ data[i]];
    }
    return ~crc;
}

static inline void parse_input_report(t_dslink *x, const unsigned char *buf, int filter) {
    int offset = x->is_bluetooth ? 2 : 1;

    // Left analog
    output_value(x->data_out, (const char*[]){"analog", "l", "x"}, 3, &x->state.analog.l.x, (buf[offset + 0] - 128) / 128.0f, filter);
    output_value(x->data_out, (const char*[]){"analog", "l", "y"}, 3, &x->state.analog.l.y, (buf[offset + 1] - 128) / -128.0f, filter);

    // Right analog
    output_value(x->data_out, (const char*[]){"analog", "r", "x"}, 3, &x->state.analog.r.x, (buf[offset + 2] - 128) / 128.0f, filter);
    output_value(x->data_out, (const char*[]){"analog", "r", "y"}, 3, &x->state.analog.r.y, (buf[offset + 3] - 128) / -128.0f, filter);

    // Triggers
    output_value(x->data_out, (const char*[]){"trigger", "l"}, 2, &x->state.trigger.l, buf[offset + 4] / 255.0f, filter);
    output_value(x->data_out, (const char*[]){"trigger", "r"}, 2, &x->state.trigger.r, buf[offset + 5] / 255.0f, filter);

    // Buttons
    output_value(x->data_out, (const char*[]){"button", "triangle"}, 2, &x->state.button.triangle, (buf[offset + 7] & 0x80) != 0, filter);
    output_value(x->data_out, (const char*[]){"button", "circle"}, 2, &x->state.button.circle, (buf[offset + 7] & 0x40) != 0, filter);
    output_value(x->data_out, (const char*[]){"button", "cross"}, 2, &x->state.button.cross, (buf[offset + 7] & 0x20) != 0, filter);
    output_value(x->data_out, (const char*[]){"button", "square"}, 2, &x->state.button.square, (buf[offset + 7] & 0x10) != 0, filter);
    output_value(x->data_out, (const char*[]){"button", "l1"}, 2, &x->state.button.l1, (buf[offset + 8] & 0x01) != 0, filter);
    output_value(x->data_out, (const char*[]){"button", "r1"}, 2, &x->state.button.r1, (buf[offset + 8] & 0x02) != 0, filter);
    output_value(x->data_out, (const char*[]){"button", "l2"}, 2, &x->state.button.l2, (buf[offset + 8] & 0x04) != 0, filter);
    output_value(x->data_out, (const char*[]){"button", "r2"}, 2, &x->state.button.r2, (buf[offset + 8] & 0x08) != 0, filter);
    output_value(x->data_out, (const char*[]){"button", "l3"}, 2, &x->state.button.l3, (buf[offset + 8] & 0x40) != 0, filter);
    output_value(x->data_out, (const char*[]){"button", "r3"}, 2, &x->state.button.r3, (buf[offset + 8] & 0x80) != 0, filter);
    output_value(x->data_out, (const char*[]){"button", "create"}, 2, &x->state.button.create, (buf[offset + 8] & 0x10) != 0, filter);
    output_value(x->data_out, (const char*[]){"button", "options"}, 2, &x->state.button.options, (buf[offset + 8] & 0x20) != 0, filter);
    output_value(x->data_out, (const char*[]){"button", "ps"}, 2, &x->state.button.ps, (buf[offset + 9] & 0x01) != 0, filter);
    output_value(x->data_out, (const char*[]){"button", "pad"}, 2, &x->state.button.pad, (buf[offset + 9] & 0x02) != 0, filter);
    output_value(x->data_out, (const char*[]){"button", "mute"}, 2, &x->state.button.mute, (buf[offset + 9] & 0x04) != 0, filter);

    // D-pad
    uint8_t digital = buf[offset + 7] & 0x0F;
    int digital_x = 0, digital_y = 0;
    switch (digital) {
        case 0: digital_y = 1; break;
        case 1: digital_x = 1; digital_y = 1; break;
        case 2: digital_x = 1; break;
        case 3: digital_x = 1; digital_y = -1; break;
        case 4: digital_y = -1; break;
        case 5: digital_x = -1; digital_y = -1; break;
        case 6: digital_x = -1; break;
        case 7: digital_x = -1; digital_y = 1; break;
    }
    output_value(x->data_out, (const char*[]){"digital", "x"}, 2, &x->state.digital.x, digital_x, filter);
    output_value(x->data_out, (const char*[]){"digital", "y"}, 2, &x->state.digital.y, digital_y, filter);

    // Gyroscope
    uint16_t gyro_x_raw = (uint16_t)((buf[offset + 17]) | buf[offset + 16] << 8);
    uint16_t gyro_y_raw = (uint16_t)((buf[offset + 19]) | buf[offset + 18] << 8);
    uint16_t gyro_z_raw = (uint16_t)((buf[offset + 21]) | buf[offset + 20] << 8);

    t_float gyro_x = (t_float)((gyro_x_raw > 32767) ? gyro_x_raw - 65536 : gyro_x_raw) / 8192.0f;
    t_float gyro_y = (t_float)((gyro_y_raw > 32767) ? gyro_y_raw - 65536 : gyro_y_raw) / 8192.0f;
    t_float gyro_z = (t_float)((gyro_z_raw > 32767) ? gyro_z_raw - 65536 : gyro_z_raw) / 8192.0f;

    output_value(x->imu_out, (const char*[]){"gyro", "x"}, 2, &x->state.gyro.x, gyro_x, filter);
    output_value(x->imu_out, (const char*[]){"gyro", "y"}, 2, &x->state.gyro.y, gyro_y, filter);
    output_value(x->imu_out, (const char*[]){"gyro", "z"}, 2, &x->state.gyro.z, gyro_z, filter);

    // Accelerometer
    uint16_t accel_x_raw = (uint16_t)((buf[offset + 23]) | buf[offset + 22] << 8);
    uint16_t accel_y_raw = (uint16_t)((buf[offset + 25]) | buf[offset + 24] << 8);
    uint16_t accel_z_raw = (uint16_t)((buf[offset + 27]) | buf[offset + 26] << 8);

    t_float accel_x = (t_float)((accel_x_raw > 32767) ? accel_x_raw - 65536 : accel_x_raw) / 8192.0f;
    t_float accel_y = (t_float)((accel_y_raw > 32767) ? accel_y_raw - 65536 : accel_y_raw) / 8192.0f;
    t_float accel_z = (t_float)((accel_z_raw > 32767) ? accel_z_raw - 65536 : accel_z_raw) / 8192.0f;

    output_value(x->imu_out, (const char*[]){"accel", "x"}, 2, &x->state.accel.x, accel_x, filter);
    output_value(x->imu_out, (const char*[]){"accel", "y"}, 2, &x->state.accel.y, accel_y, filter);
    output_value(x->imu_out, (const char*[]){"accel", "z"}, 2, &x->state.accel.z, accel_z, filter);

    // Touchpad
    for (int i = 0; i < 2; i++) {
        int touch_offset = offset + 32 + (i * 4);
        uint8_t touch_data1 = buf[touch_offset];
        uint8_t touch_data2 = buf[touch_offset + 1];
        uint8_t touch_data3 = buf[touch_offset + 2];
        uint8_t touch_data4 = buf[touch_offset + 3];
        int is_active = !(touch_data1 & 0x80);

        const char* touch_id = (i == 0) ? "touch1" : "touch2";

        if (is_active) {
            int touch_x = ((touch_data3 & 0x0F) << 8) | touch_data2;
            int touch_y = (touch_data4 << 4) | ((touch_data3 & 0xF0) >> 4);
            
            output_value(x->data_out, (const char*[]){"pad", touch_id, "active"}, 3,
                strcmp(touch_id, "touch1") == 0 ? &x->state.touch1.active : &x->state.touch2.active, 1.0f, filter);
            output_value(x->data_out, (const char*[]){"pad", touch_id, "x"}, 3,
                strcmp(touch_id, "touch1") == 0 ? &x->state.touch1.active : &x->state.touch2.x, touch_x / 1920.0f, filter);
            output_value(x->data_out, (const char*[]){"pad", touch_id, "y"}, 3,
                strcmp(touch_id, "touch1") == 0 ? &x->state.touch1.active : &x->state.touch2.y, touch_y / 1080.0f, filter);
        } else {
            output_value(x->data_out, (const char*[]){"pad", touch_id, "active"}, 3,
                strcmp(touch_id, "touch1") == 0 ? &x->state.touch1.active : &x->state.touch2.active, 0.0f, filter);
        }
    }

    // Battery level and status
    uint8_t battery_data = buf[offset + 52];
    float battery_level = (int)battery_data & 0x0F;

    output_value(x->status_out, (const char*[]){"battery", "level"}, 2, &x->state.battery_level, battery_level, filter);


    // For battery status
    float battery_status;
    switch ((buf[offset + 52] & 0xF0) >> 4) {
        case 0x0: battery_status = (float)BATTERY_DISCHARGING; break;
        case 0x1: battery_status = (float)BATTERY_CHARGING; break;
        case 0x2: battery_status = (float)BATTERY_FULL; break;
        case 0xA: battery_status = (float)BATTERY_TEMP_HIGH; break;
        case 0xB: battery_status = (float)BATTERY_TEMP_LOW; break;
        default: battery_status = (float)BATTERY_UNKNOWN;
    }

    output_value(x->status_out, (const char*[]){"battery", "status"}, 2, &x->state.battery_status, battery_status, filter);

    // Connection type (already known from device opening)
    output_value(x->status_out, (const char*[]){"bluetooth"}, 1, &x->state.bluetooth, x->is_bluetooth ? 1.0f : 0.0f, filter);

    // Headphone and mic status
    uint8_t headset_data = buf[offset + 53];
    output_value(x->status_out, (const char*[]){"headphones"}, 1, &x->state.headphones, (headset_data & 0x01) != 0, filter);
    output_value(x->status_out, (const char*[]){"microphone"}, 1, &x->state.microphone, (headset_data & 0x02) != 0, filter);

    // Haptic feedback motors active status
    output_value(x->status_out, (const char*[]){"haptic", "active"}, 2, &x->state.haptic_active, (buf[offset + 54] & 0x02) != 0, filter); // FIXME: check
}


static void dslink_free(t_dslink *x) {
    if (x->handle) hid_close(x->handle);

    clock_unset(x->poll_clock);
    clock_unset(x->write_clock);
    clock_unset(x->open_clock);

    if (x->poll_clock) {
        clock_free(x->poll_clock);
        x->poll_clock = NULL;
    }
    if (x->write_clock) {
        clock_free(x->write_clock);
        x->write_clock = NULL;
    }
    if (x->open_clock) {
        clock_free(x->open_clock);
        x->open_clock = NULL;
    }

    hid_exit();
}

static void *dslink_new(void) {
    t_dslink *x = (t_dslink *)pd_new(dslink_class);
    x->write_size = 0;

    x->data_out = outlet_new(&x->x_obj, &s_anything);
    x->imu_out = outlet_new(&x->x_obj, &s_anything);
    x->status_out = outlet_new(&x->x_obj, &s_anything);

    x->open_clock = clock_new(x, (t_method)open_tick);
    x->poll_clock = clock_new(x, (t_method)poll_tick);
    x->write_clock = clock_new(x, (t_method)do_write);
    x->poll_interval = 0;
    x->handle = NULL;

    memset(&x->state, 0, sizeof(t_dslink_state));
    post("dslink: trying to connect ...");
    clock_delay(x->open_clock, 0);
    return (void *)x;
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
    class_addmethod(dslink_class, (t_method)dslink_state, gensym("state"), 0);
    class_addmethod(dslink_class, (t_method)dslink_close, gensym("close"), 0);
    class_addmethod(dslink_class, (t_method)dslink_poll, gensym("poll"), A_FLOAT, 0);
    class_addmethod(dslink_class, (t_method)dslink_set_motor, gensym("motor"), A_SYMBOL, A_FLOAT, 0);
    class_addmethod(dslink_class, (t_method)dslink_configure, gensym("configure"), A_FLOAT, 0);
    class_addmethod(dslink_class, (t_method)dslink_set_led, gensym("led"), A_GIMME, 0);
    class_addmethod(dslink_class, (t_method)dslink_set_trigger, gensym("trigger"), A_GIMME, 0);

    post("\n  dslink v%d.%d.%d", DSLINK_MAJOR_VERSION, DSLINK_MINOR_VERSION, DSLINK_BUGFIX_VERSION);
    post(  "  hidapi v%d.%d.%d\n", HID_API_VERSION_MAJOR, HID_API_VERSION_MINOR, HID_API_VERSION_PATCH);

    generate_crc32_table();
    hid_init();

#if defined(__APPLE__)
    // To work properly needs to be called before hid_open/hid_open_path after hid_init.
    // Best/recommended option - call it right after hid_init.
    hid_darwin_set_open_exclusive(0);
#endif
}
