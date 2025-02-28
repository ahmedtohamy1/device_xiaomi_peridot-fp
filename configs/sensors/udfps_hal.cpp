/*
 * Copyright (C) 2022 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "sensors.udfps.peridot"

#include <errno.h>
#include <fcntl.h>
#include <hardware/sensors.h>
#include <log/log.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <utils/SystemClock.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdlib.h>

// For screen wake-up
#define POWER_STATUS_PATH "/sys/class/power_supply/battery/status"
#define BACKLIGHT_PATH "/sys/class/backlight/panel0-backlight/brightness"

// Debounce time in nanoseconds (1 second here)
#define WAKEUP_DEBOUNCE_NS 1000000000LL

static const char *udfps_state_paths[] = {
        "/sys/devices/virtual/touch/tp_dev/fod_press_status",
        "/sys/devices/virtual/touch/tp_dev/touch_finger_status",
        NULL,
};

static struct sensor_t udfps_sensor = {
        .name = "UDFPS Sensor",
        .vendor = "The Yet Another AOSP Project",
        .version = 1,
        .handle = 0,
        .type = SENSOR_TYPE_DEVICE_PRIVATE_BASE + 1,
        .maxRange = 2048.0f,
        .resolution = 1.0f,
        .power = 0,
        .minDelay = -1,
        .fifoReservedEventCount = 0,
        .fifoMaxEventCount = 0,
        .stringType = "org.yaap.sensor.udfps",
        .requiredPermission = "",
        .maxDelay = 0,
        .flags = SENSOR_FLAG_ONE_SHOT_MODE | SENSOR_FLAG_WAKE_UP,
        .reserved = {},
};

struct udfps_context_t {
    sensors_poll_device_1_t device;
    int fd;
};

// Check if screen is on by verifying backlight brightness
static bool isScreenOn() {
    int fd = open(BACKLIGHT_PATH, O_RDONLY);
    if (fd < 0) {
        ALOGE("isScreenOn: Failed to open backlight path: %d", -errno);
        return false;
    }
    char buf[16] = {0};
    int rc = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (rc < 0) {
        ALOGE("isScreenOn: Failed to read backlight: %d", -errno);
        return false;
    }
    int brightness = atoi(buf);
    ALOGI("isScreenOn: Backlight brightness = %d", brightness);
    return brightness > 0;
}

// Helper: Try to open one of several input event devices.
static int open_input_device() {
    const char *device_paths[] = { "/dev/input/event0", "/dev/input/event1", "/dev/input/event2", NULL };
    int fd = -1;
    for (int i = 0; device_paths[i]; i++) {
        fd = open(device_paths[i], O_WRONLY);
        if (fd >= 0) {
            ALOGI("wakeUpScreen: Opened input device: %s", device_paths[i]);
            break;
        } else {
            ALOGW("wakeUpScreen: Unable to open input device %s: %d", device_paths[i], -errno);
        }
    }
    return fd;
}

// Wake up the screen using multiple fallback methods with a debounce guard.
static void wakeUpScreen() {
    static int64_t last_wakeup_ns = 0;
    int64_t now_ns = ::android::elapsedRealtimeNano();

    // If a wake-up was sent less than WAKEUP_DEBOUNCE_NS ago, skip.
    if (now_ns - last_wakeup_ns < WAKEUP_DEBOUNCE_NS) {
        ALOGI("wakeUpScreen: Debouncing wakeup; last wakeup was %lld ns ago", now_ns - last_wakeup_ns);
        return;
    }
    last_wakeup_ns = now_ns;
    
    ALOGI("wakeUpScreen: Attempting to wake up the screen");

    // Method 1: Send a key event to wake up the screen
    int fd = open_input_device();
    if (fd >= 0) {
        struct input_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = 1;      // EV_KEY
        ev.code = 143;    // KEY_WAKEUP
        ev.value = 1;     // Key press
        if (write(fd, &ev, sizeof(ev)) < 0) {
            ALOGE("wakeUpScreen: Failed to write key press event: %d", -errno);
        }
        // Send key release
        ev.value = 0;
        if (write(fd, &ev, sizeof(ev)) < 0) {
            ALOGE("wakeUpScreen: Failed to write key release event: %d", -errno);
        }
        close(fd);
        ALOGI("wakeUpScreen: Sent wakeup key event via input device");
    } else {
        ALOGE("wakeUpScreen: All input device attempts failed");
    }
    
    // Method 2: Write directly to sysfs (if supported)
    fd = open("/sys/power/state", O_WRONLY);
    if (fd >= 0) {
        if (write(fd, "on", 2) < 0) {
            ALOGE("wakeUpScreen: Failed to write 'on' to /sys/power/state: %d", -errno);
        } else {
            ALOGI("wakeUpScreen: Wrote 'on' to /sys/power/state");
        }
        close(fd);
    } else {
        ALOGW("wakeUpScreen: Could not open /sys/power/state: %d", -errno);
    }
    
    // Method 3: Fallback to system command
    int ret = system("input keyevent KEYCODE_WAKEUP");
    if (ret != 0) {
        ALOGE("wakeUpScreen: System command 'input keyevent KEYCODE_WAKEUP' failed with return %d", ret);
    } else {
        ALOGI("wakeUpScreen: Executed system command 'input keyevent KEYCODE_WAKEUP'");
    }
}

// Helper function to read a full line from a file descriptor.
static int udfps_read_line(int fd, char* buf, size_t len) {
    int rc = lseek(fd, 0, SEEK_SET);
    if (rc < 0) {
        ALOGE("udfps_read_line: Failed to seek: %d", -errno);
        return rc;
    }
    rc = read(fd, buf, len);
    if (rc < 0) {
        ALOGE("udfps_read_line: Failed to read: %d", -errno);
        return rc;
    }
    return rc;
}

// Parse the FOD state and positions from the file descriptor.
static int udfps_read_state(int fd, int& pos_x, int& pos_y) {
    int rc, state = 0;
    char buf[64] = {0};
    rc = udfps_read_line(fd, buf, sizeof(buf) - 1);
    if (rc > 0) {
        rc = sscanf(buf, "%d,%d,%d", &pos_x, &pos_y, &state);
        if (rc != 3) {
            ALOGE("udfps_read_state: Failed to parse fp_state (rc=%d): %s", rc, buf);
            state = 0;
        } else {
            ALOGI("udfps_read_state: Parsed values: pos_x=%d, pos_y=%d, state=%d", pos_x, pos_y, state);
        }
    }
    return state;
}

// Wait for an event on the file descriptor.
static int udfps_wait_event(int fd, int timeout) {
    struct pollfd fds = {
            .fd = fd,
            .events = POLLERR | POLLPRI,
            .revents = 0,
    };
    int rc;
    do {
        rc = poll(&fds, 1, timeout);
    } while (rc < 0 && errno == EINTR);
    return rc;
}

// Flush pending events.
static void udfps_flush_events(int fd) {
    char buf[64];
    while (udfps_wait_event(fd, 0) > 0) {
        udfps_read_line(fd, buf, sizeof(buf));
    }
}

static int udfps_close(struct hw_device_t* dev) {
    udfps_context_t* ctx = reinterpret_cast<udfps_context_t*>(dev);
    if (ctx) {
        close(ctx->fd);
        delete ctx;
    }
    return 0;
}

static int udfps_activate(struct sensors_poll_device_t* dev, int handle, int enabled) {
    udfps_context_t* ctx = reinterpret_cast<udfps_context_t*>(dev);
    if (!ctx || handle) {
        return -EINVAL;
    }
    // Flush any pending events when enabling
    if (enabled) {
        ALOGI("udfps_activate: Flushing events on enable");
        udfps_flush_events(ctx->fd);
    }
    return 0;
}

static int udfps_setDelay(struct sensors_poll_device_t* dev, int handle, int64_t /* ns */) {
    udfps_context_t* ctx = reinterpret_cast<udfps_context_t*>(dev);
    if (!ctx || handle) {
        return -EINVAL;
    }
    return 0;
}

static int udfps_poll(struct sensors_poll_device_t* dev, sensors_event_t* data, int /* count */) {
    udfps_context_t* ctx = reinterpret_cast<udfps_context_t*>(dev);
    if (!ctx) {
        return -EINVAL;
    }
    int fod_x = 0, fod_y = 0, fod_state = 0;
    // Wait for a non-zero FOD state
    do {
        int rc = udfps_wait_event(ctx->fd, -1);
        if (rc < 0) {
            ALOGE("udfps_poll: Failed to poll fp_state: %d", -errno);
            return -errno;
        } else if (rc > 0) {
            fod_state = udfps_read_state(ctx->fd, fod_x, fod_y);
        }
    } while (!fod_state);
    // If a finger is detected and the screen is off, wake up the screen.
    if (fod_state && !isScreenOn()) {
        ALOGI("udfps_poll: Finger detected while screen is off, initiating wake up");
        wakeUpScreen();
    }
    memset(data, 0, sizeof(sensors_event_t));
    data->version = sizeof(sensors_event_t);
    data->sensor = udfps_sensor.handle;
    data->type = udfps_sensor.type;
    data->timestamp = ::android::elapsedRealtimeNano();
    data->data[0] = fod_x;
    data->data[1] = fod_y;
    return 1;
}

static int udfps_batch(struct sensors_poll_device_1* /* dev */, int /* handle */, int /* flags */,
                       int64_t /* period_ns */, int64_t /* max_ns */) {
    return 0;
}

static int udfps_flush(struct sensors_poll_device_1* /* dev */, int /* handle */) {
    return -EINVAL;
}

static int open_sensors(const struct hw_module_t* module, const char* /* name */,
                        struct hw_device_t** device) {
    udfps_context_t* ctx = new udfps_context_t();
    memset(ctx, 0, sizeof(udfps_context_t));
    ctx->device.common.tag = HARDWARE_DEVICE_TAG;
    ctx->device.common.version = SENSORS_DEVICE_API_VERSION_1_3;
    ctx->device.common.module = const_cast<hw_module_t*>(module);
    ctx->device.common.close = udfps_close;
    ctx->device.activate = udfps_activate;
    ctx->device.setDelay = udfps_setDelay;
    ctx->device.poll = udfps_poll;
    ctx->device.batch = udfps_batch;
    ctx->device.flush = udfps_flush;
    for (int i = 0; udfps_state_paths[i]; i++) {
        ctx->fd = open(udfps_state_paths[i], O_RDONLY);
        if (ctx->fd >= 0) {
            ALOGI("open_sensors: Opened FOD state file: %s", udfps_state_paths[i]);
            break;
        } else {
            ALOGW("open_sensors: Failed to open %s: %d", udfps_state_paths[i], -errno);
        }
    }
    if (ctx->fd < 0) {
        ALOGE("open_sensors: Failed to open any FOD state file: %d", -errno);
        delete ctx;
        return -ENODEV;
    }
    *device = &ctx->device.common;
    return 0;
}

static struct hw_module_methods_t udfps_module_methods = {
        .open = open_sensors,
};

static int udfps_get_sensors_list(struct sensors_module_t*, struct sensor_t const** list) {
    *list = &udfps_sensor;
    return 1;
}

static int udfps_set_operation_mode(unsigned int mode) {
    return (mode == 0) ? 0 : -EINVAL;
}

struct sensors_module_t HAL_MODULE_INFO_SYM = {
        .common = {.tag = HARDWARE_MODULE_TAG,
                   .version_major = 1,
                   .version_minor = 0,
                   .id = SENSORS_HARDWARE_MODULE_ID,
                   .name = "UDFPS Sensor module",
                   .author = "Ivan Vecera",
                   .methods = &udfps_module_methods,
                   .dso = NULL,
                   .reserved = {0}},
        .get_sensors_list = udfps_get_sensors_list,
        .set_operation_mode = udfps_set_operation_mode,
};
