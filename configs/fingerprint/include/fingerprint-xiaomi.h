/**
 * Copyright (C) 2025 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <hardware/hardware.h>
#include <hardware/hw_auth_token.h>

#define COMMAND_NIT 10
#define PARAM_NIT_FOD 1
#define PARAM_NIT_NONE 0

#define COMMAND_FOD_PRESS_STATUS 1
#define COMMAND_FOD_PRESS_X 2
#define COMMAND_FOD_PRESS_Y 3
#define PARAM_FOD_PRESSED 1
#define PARAM_FOD_RELEASED 0

#define FOD_STATUS_PATH "/sys/class/touch/touch_dev/fod_press_status"
#define FOD_STATUS_OFF 0
#define FOD_STATUS_ON 1

#define DISP_PARAM_PATH "/sys/devices/virtual/mi_display/disp_feature/disp-DSI-0/disp_param"
#define DISP_PARAM_LOCAL_HBM_MODE "9"
#define DISP_PARAM_LOCAL_HBM_OFF "0"
#define DISP_PARAM_LOCAL_HBM_ON "1"

#define FINGERPRINT_ACQUIRED_VENDOR 7

typedef struct fingerprint_hal {
    const char* class_name;
} fingerprint_hal_t;

static const fingerprint_hal_t kModules[] = {
    {"fpc"},{"fpc_fod"},{"goodix"},
    {"goodix_fod"}, {"goodix_us"},
};

#define FINGERPRINT_MODULE_API_VERSION_2_1 HARDWARE_MODULE_API_VERSION(2, 1)
#define FINGERPRINT_HARDWARE_MODULE_ID "fingerprint"

/***********************************************************
 * Standard fingerprint structures and enums
 ***********************************************************/
typedef enum fingerprint_msg_type {
    FINGERPRINT_ERROR = -1,
    FINGERPRINT_ACQUIRED = 1,
    FINGERPRINT_TEMPLATE_ENROLLING = 3,
    FINGERPRINT_TEMPLATE_REMOVED = 4,
    FINGERPRINT_AUTHENTICATED = 5,
    FINGERPRINT_TEMPLATE_ENUMERATING = 6,
    FINGERPRINT_CHALLENGE_GENERATED = 7,
    FINGERPRINT_CHALLENGE_REVOKED = 8,
    FINGERPRINT_AUTHENTICATOR_ID_RETRIEVED = 9,
    FINGERPRINT_AUTHENTICATOR_ID_INVALIDATED = 10,
    FINGERPRINT_RESET_LOCKOUT = 11,
} fingerprint_msg_type_t;

typedef enum fingerprint_error {
    FINGERPRINT_ERROR_HW_UNAVAILABLE = 1,
    FINGERPRINT_ERROR_UNABLE_TO_PROCESS = 2,
    FINGERPRINT_ERROR_TIMEOUT = 3,
    FINGERPRINT_ERROR_NO_SPACE = 4,
    FINGERPRINT_ERROR_CANCELED = 5,
    FINGERPRINT_ERROR_UNABLE_TO_REMOVE = 6,
    FINGERPRINT_ERROR_LOCKOUT = 7,
    FINGERPRINT_ERROR_VENDOR_BASE = 1000
} fingerprint_error_t;

typedef enum fingerprint_acquired_info {
    FINGERPRINT_ACQUIRED_GOOD = 0,
    FINGERPRINT_ACQUIRED_PARTIAL = 1,
    FINGERPRINT_ACQUIRED_INSUFFICIENT = 2,
    FINGERPRINT_ACQUIRED_IMAGER_DIRTY = 3,
    FINGERPRINT_ACQUIRED_TOO_SLOW = 4,
    FINGERPRINT_ACQUIRED_TOO_FAST = 5,
    FINGERPRINT_ACQUIRED_DETECTED = 6,
    FINGERPRINT_ACQUIRED_VENDOR_BASE = 1000
} fingerprint_acquired_info_t;

typedef struct fingerprint_finger_id {
    uint32_t fid;
} fingerprint_finger_id_t;

typedef struct fingerprint_enroll {
    uint32_t fid;
    uint32_t samples_remaining;
    uint64_t msg;
} fingerprint_enroll_t;

typedef struct fingerprint_iterator {
    uint32_t fid;
    uint32_t remaining_templates;
} fingerprint_iterator_t;

typedef fingerprint_iterator_t fingerprint_enumerated_t;
typedef fingerprint_iterator_t fingerprint_removed_t;

typedef struct fingerprint_acquired {
    fingerprint_acquired_info_t acquired_info;
} fingerprint_acquired_t;

typedef struct fingerprint_authenticated {
    fingerprint_finger_id_t finger;
    hw_auth_token_t hat;
} fingerprint_authenticated_t;

typedef struct fingerprint_vendor_extend {
    int64_t data;
} fingerprint_vendor_extend_t;

typedef struct fingerprint_msg {
    fingerprint_msg_type_t type;
    union {
        fingerprint_error_t error;
        fingerprint_enroll_t enroll;
        fingerprint_enumerated_t enumerated;
        fingerprint_removed_t removed;
        fingerprint_acquired_t acquired;
        fingerprint_authenticated_t authenticated;
        fingerprint_vendor_extend_t extend;
    } data;
} fingerprint_msg_t;

/* Callback function type */
typedef void (*fingerprint_notify_t)(const fingerprint_msg_t *msg);

typedef struct fingerprint_device {
    struct hw_device_t common;

    fingerprint_notify_t notify;

    int (*set_notify)(struct fingerprint_device *dev, fingerprint_notify_t notify);

    /* Challenge */
    uint64_t (*generateChallenge)(struct fingerprint_device *dev);
    uint32_t (*revokeChallenge)(struct fingerprint_device *dev, uint64_t challenge);

    /* Enrollment */
    uint32_t (*enroll)(struct fingerprint_device *dev, const hw_auth_token_t *hat);

    /* Authenticator ID */
    uint64_t (*getAuthenticatorId)(struct fingerprint_device *dev);
    uint64_t (*invalidateAuthenticatorId)(struct fingerprint_device *dev);

    /* Cancel current operation */
    uint32_t (*cancel)(struct fingerprint_device *dev);

    /* Enumerate fingerprints */
    uint32_t (*enumerate)(struct fingerprint_device *dev);

    /* Remove fingerprints */
    uint64_t (*remove)(struct fingerprint_device *dev, const int32_t *enrollmentIds, uint32_t count);

    /* Set current user / group */
    uint32_t (*setActiveGroup)(struct fingerprint_device *dev, uint32_t userid, const char *store_path);

    /* Authentication flow */
    uint32_t (*authenticate)(struct fingerprint_device *dev, uint64_t operation_id);

    /* Reset lockout */
    uint32_t (*resetLockout)(struct fingerprint_device* dev, const hw_auth_token_t* hat);

    /* For under-display sensors (not used by Xiaomi’s goodix, typically) */
    void (*onPointerDown)(struct fingerprint_device *dev, int32_t pointerId, int32_t x, int32_t y, float minor, float major);
    void (*onPointerUp)(struct fingerprint_device *dev, int32_t pointerId);

    /* Xiaomi/Goodix vendor extension */
    uint64_t (*goodixExtCmd)(struct fingerprint_device *dev, int32_t cmd, int32_t param);

    void *reserved[2];
} fingerprint_device_t;

typedef struct fingerprint_module {
    struct hw_module_t common;
} fingerprint_module_t;
