#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef esp_err_t (*ble_wifi_save_callback_t)(
    const char *ssid,
    const char *password
);

esp_err_t ble_provisioning_start(ble_wifi_save_callback_t save_callback);
bool ble_provisioning_is_running(void);
void ble_provisioning_set_wifi_connected(bool connected, const char *ip);