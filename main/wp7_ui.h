#pragma once

#include <stdbool.h>

typedef enum {
    WP7_KEY_UP,
    WP7_KEY_DOWN,
    WP7_KEY_OK,
} wp7_key_t;

typedef enum {
    WP7_WIFI_DISCONNECTED = 0,
    WP7_WIFI_CONNECTING,
    WP7_WIFI_CONNECTED,
} wp7_wifi_status_t;

typedef enum {
    WP7_BLE_DISABLED = 0,
    WP7_BLE_ADVERTISING,
    WP7_BLE_CONNECTED,
} wp7_ble_status_t;

/* Caller holds the LVGL port lock for both functions. */
void wp7_ui_start(void);
void wp7_ui_key(wp7_key_t key, bool long_press);
void wp7_ui_set_battery(int soc, int mv);
void wp7_ui_set_wifi(wp7_wifi_status_t status);
void wp7_ui_set_ble(wp7_ble_status_t status);
