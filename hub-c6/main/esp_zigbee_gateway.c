/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: LicenseRef-Included
 */
/* Zigbee Gateway Example - Modificado para hub_c6 do TCC */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_zigbee_core.h"
#include "zcl/esp_zigbee_zcl_command.h"
#include "zcl/esp_zigbee_zcl_on_off.h"
#include "zdo/esp_zigbee_zdo_command.h"
#include "driver/uart.h"
#include "cJSON.h"

/* Zigbee Configuration */
#define MAX_CHILDREN 10
#define INSTALLCODE_POLICY_ENABLE false
#define ESP_ZB_PRIMARY_CHANNEL_MASK (1L << 13)
#define ESP_ZB_GATEWAY_ENDPOINT 1

/* UART Configuration */
#define UART_PORT_NUM     UART_NUM_1
#define UART_TX_GPIO      4
#define UART_RX_GPIO      5
#define UART_BAUD_RATE    115200

static const char *TAG = "ESP_ZB_GATEWAY";
static const char *TAG_UART = "UART";

static uint16_t light_short_address = 0xb5f5;
static uint8_t light_endpoint = 0;
static bool light_endpoint_discovered = false;
static esp_zb_ieee_addr_t light_ieee_address = {0};
static const char *const discovered_device_id = "luz_sala";
static SemaphoreHandle_t uart_tx_mutex;

static bool uart_send_json(const char *serialized)
{
    if (uart_tx_mutex == NULL ||
        xSemaphoreTake(uart_tx_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG_UART, "UART TX ocupado");
        return false;
    }

    const size_t length = strlen(serialized);
    const int payload_written = uart_write_bytes(UART_PORT_NUM, serialized, length);
    bool success = payload_written == (int)length;
    if (success) {
        success = uart_write_bytes(UART_PORT_NUM, "\n", 1) == 1;
    }
    uart_wait_tx_done(UART_PORT_NUM, pdMS_TO_TICKS(1000));
    xSemaphoreGive(uart_tx_mutex);
    return success;
}

static void uart_send_response(int request_id, const char *device, const char *status, const char *error)
{
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "type", "response");
    cJSON_AddNumberToObject(response, "request_id", request_id);
    cJSON_AddStringToObject(response, "device", device);
    cJSON_AddStringToObject(response, "status", status);
    if (error != NULL) {
        cJSON_AddStringToObject(response, "error", error);
    }

    char *serialized = cJSON_PrintUnformatted(response);
    if (serialized != NULL) {
        uart_send_json(serialized);
        free(serialized);
    }
    cJSON_Delete(response);
}

static void uart_send_device_discovered_event(void)
{
    cJSON *message = cJSON_CreateObject();
    cJSON_AddStringToObject(message, "type", "event");
    cJSON_AddStringToObject(message, "device", discovered_device_id);
    cJSON_AddStringToObject(message, "event", "device_discovered");
    cJSON_AddStringToObject(message, "value", "on_off");
    cJSON_AddNumberToObject(message, "short_address", light_short_address);
    cJSON_AddNumberToObject(message, "endpoint", light_endpoint);

    char ieee[24];
    snprintf(ieee, sizeof(ieee), "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
             light_ieee_address[7], light_ieee_address[6],
             light_ieee_address[5], light_ieee_address[4],
             light_ieee_address[3], light_ieee_address[2],
             light_ieee_address[1], light_ieee_address[0]);
    cJSON_AddStringToObject(message, "ieee_address", ieee);

    char *serialized = cJSON_PrintUnformatted(message);
    if (serialized != NULL) {
        ESP_LOGI(TAG_UART, "Enviando evento (%u bytes): %s",
                 (unsigned)strlen(serialized), serialized);
        uart_send_json(serialized);
        free(serialized);
    }
    cJSON_Delete(message);
}

static void uart_send_state_event(uint16_t short_address, uint8_t endpoint, bool state)
{
    cJSON *message = cJSON_CreateObject();
    cJSON_AddStringToObject(message, "type", "event");
    cJSON_AddStringToObject(message, "device", discovered_device_id);
    cJSON_AddStringToObject(message, "event", "state_changed");
    cJSON_AddBoolToObject(message, "value", state);
    cJSON_AddNumberToObject(message, "short_address", short_address);
    cJSON_AddNumberToObject(message, "endpoint", endpoint);

    char *serialized = cJSON_PrintUnformatted(message);
    if (serialized != NULL) {
        ESP_LOGI(TAG_UART, "Enviando estado (%u bytes): %s",
                 (unsigned)strlen(serialized), serialized);
        uart_send_json(serialized);
        free(serialized);
    }
    cJSON_Delete(message);
}

static void simple_desc_callback(esp_zb_zdp_status_t status,
                                 esp_zb_af_simple_desc_1_1_t *simple_desc,
                                 void *user_ctx)
{
    if (status != ESP_ZB_ZDP_STATUS_SUCCESS || simple_desc == NULL) {
        ESP_LOGW(TAG, "Simple descriptor request failed: status=%d", status);
        return;
    }

    ESP_LOGI(TAG, "Device 0x%04x endpoint %u profile=0x%04x device=0x%04x",
             light_short_address, simple_desc->endpoint,
             simple_desc->app_profile_id, simple_desc->app_device_id);

    const uint8_t cluster_count = simple_desc->app_input_cluster_count +
                                  simple_desc->app_output_cluster_count;
    bool has_on_off = false;
    for (uint8_t i = 0; i < cluster_count; ++i) {
        const uint16_t cluster_id = simple_desc->app_cluster_list[i];
        ESP_LOGI(TAG, "Device 0x%04x endpoint %u cluster[%u]=0x%04x",
                 light_short_address, simple_desc->endpoint, i, cluster_id);
        if (cluster_id == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF) {
            has_on_off = true;
        }
    }

    if (has_on_off && !light_endpoint_discovered && simple_desc->endpoint != 0xf2) {
        light_endpoint = simple_desc->endpoint;
        light_endpoint_discovered = true;
        ESP_LOGI(TAG, "On/Off cluster found at endpoint %u", light_endpoint);
        uart_send_device_discovered_event();
    }
}

static void active_ep_callback(esp_zb_zdp_status_t status, uint8_t ep_count,
                               uint8_t *ep_id_list, void *user_ctx)
{
    if (status != ESP_ZB_ZDP_STATUS_SUCCESS || ep_id_list == NULL) {
        ESP_LOGW(TAG, "Active endpoint request failed: status=%d", status);
        return;
    }

    ESP_LOGI(TAG, "Device 0x%04x has %u active endpoint(s)",
             light_short_address, ep_count);
    for (uint8_t i = 0; i < ep_count; ++i) {
        esp_zb_zdo_simple_desc_req_param_t request = {
            .addr_of_interest = light_short_address,
            .endpoint = ep_id_list[i],
        };
        esp_zb_zdo_simple_desc_req(&request, simple_desc_callback, NULL);
    }
}

static void discover_device(uint16_t short_address)
{
    light_short_address = short_address;
    light_endpoint = 0;
    light_endpoint_discovered = false;
    esp_zb_zdo_active_ep_req_param_t request = {
        .addr_of_interest = short_address,
    };
    esp_zb_zdo_active_ep_req(&request, active_ep_callback, NULL);
}

static esp_err_t zigbee_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    if (callback_id == ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID && message != NULL) {
        const esp_zb_zcl_set_attr_value_message_t *attribute_message =
            (const esp_zb_zcl_set_attr_value_message_t *)message;
        if (attribute_message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS &&
            attribute_message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF &&
            attribute_message->attribute.id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID &&
            attribute_message->attribute.data.value != NULL) {
            const bool state = *(bool *)attribute_message->attribute.data.value;
            ESP_LOGI(TAG, "Received On/Off state: src=0x%04x endpoint=%u state=%s",
                     light_short_address, attribute_message->info.dst_endpoint,
                     state ? "on" : "off");
            uart_send_state_event(light_short_address, attribute_message->info.dst_endpoint, state);
        }
    } else if (callback_id == ESP_ZB_CORE_CMD_DEFAULT_RESP_CB_ID && message != NULL) {
        const esp_zb_zcl_cmd_info_t *info = (const esp_zb_zcl_cmd_info_t *)message;
        ESP_LOGI(TAG, "Zigbee command response: cluster=0x%04x src=0x%04x endpoint=%u command=%u",
                 info->cluster, info->src_address.u.short_addr, info->src_endpoint, info->command.id);
    }
    return ESP_OK;
}

static bool zigbee_send_on_off(const char *action)
{
    uint8_t command_id;
    if (strcmp(action, "on") == 0) {
        command_id = ESP_ZB_ZCL_CMD_ON_OFF_ON_ID;
    } else if (strcmp(action, "off") == 0) {
        command_id = ESP_ZB_ZCL_CMD_ON_OFF_OFF_ID;
    } else {
        return false;
    }

    if (!light_endpoint_discovered) {
        ESP_LOGW(TAG, "No On/Off endpoint discovered");
        return false;
    }

    if (!esp_zb_lock_acquire(pdMS_TO_TICKS(1000))) {
        ESP_LOGE(TAG, "Could not acquire Zigbee lock");
        return false;
    }

    esp_zb_zcl_on_off_cmd_t command = {
        .zcl_basic_cmd = {
            .dst_addr_u.addr_short = light_short_address,
            .dst_endpoint = light_endpoint,
            .src_endpoint = ESP_ZB_GATEWAY_ENDPOINT,
        },
        .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        .on_off_cmd_id = command_id,
    };
    esp_zb_zcl_on_off_cmd_req(&command);
    esp_zb_lock_release();
    return true;
}

#define ESP_ZB_ZC_CONFIG() \
    { \
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_COORDINATOR, \
        .install_code_policy = INSTALLCODE_POLICY_ENABLE, \
        .nwk_cfg.zczr_cfg = { \
            .max_children = MAX_CHILDREN, \
        }, \
    }

#define ESP_ZB_DEFAULT_RADIO_CONFIG() \
    { \
        .radio_mode = ZB_RADIO_MODE_NATIVE, \
    }

#define ESP_ZB_DEFAULT_HOST_CONFIG() \
    { \
        .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE, \
    }

static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    ESP_RETURN_ON_FALSE(esp_zb_bdb_start_top_level_commissioning(mode_mask) == ESP_OK, , TAG, "Failed to start Zigbee bdb commissioning");
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;
    esp_zb_zdo_signal_device_annce_params_t *dev_annce_params = NULL;

    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Initialize Zigbee stack");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;

    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "Device started up in %s factory-reset mode", esp_zb_bdb_is_factory_new() ? "" : "non");
            if (esp_zb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "Start network formation");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_FORMATION);
            } else {
                esp_zb_bdb_open_network(180);
                ESP_LOGI(TAG, "Device rebooted");
            }
        } else {
            ESP_LOGE(TAG, "Failed to initialize Zigbee stack (status: %s)", esp_err_to_name(err_status));
        }
        break;

    case ESP_ZB_BDB_SIGNAL_FORMATION:
        if (err_status == ESP_OK) {
            esp_zb_ieee_addr_t ieee_address;
            esp_zb_get_long_address(ieee_address);
            ESP_LOGI(TAG, "Formed network successfully (Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, PAN ID: 0x%04hx, Channel:%d, Short Address: 0x%04hx)",
                     ieee_address[7], ieee_address[6], ieee_address[5], ieee_address[4],
                     ieee_address[3], ieee_address[2], ieee_address[1], ieee_address[0],
                     esp_zb_get_pan_id(), esp_zb_get_current_channel(), esp_zb_get_short_address());
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        } else {
            ESP_LOGI(TAG, "Restart network formation (status: %s)", esp_err_to_name(err_status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, ESP_ZB_BDB_MODE_NETWORK_FORMATION, 1000);
        }
        break;

    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "Network steering started");
        }
        break;

    case ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE:
        dev_annce_params = (esp_zb_zdo_signal_device_annce_params_t *)esp_zb_app_signal_get_params(p_sg_p);
        ESP_LOGI(TAG, "New device commissioned or rejoined (short: 0x%04hx, ieee: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x)",
                 dev_annce_params->device_short_addr,
                 dev_annce_params->ieee_addr[7], dev_annce_params->ieee_addr[6],
                 dev_annce_params->ieee_addr[5], dev_annce_params->ieee_addr[4],
                 dev_annce_params->ieee_addr[3], dev_annce_params->ieee_addr[2],
                 dev_annce_params->ieee_addr[1], dev_annce_params->ieee_addr[0]);
        memcpy(light_ieee_address, dev_annce_params->ieee_addr, sizeof(light_ieee_address));
        discover_device(dev_annce_params->device_short_addr);
        break;

    case ESP_ZB_NWK_SIGNAL_PERMIT_JOIN_STATUS:
        if (err_status == ESP_OK) {
            if (*(uint8_t *)esp_zb_app_signal_get_params(p_sg_p)) {
                ESP_LOGI(TAG, "Network(0x%04hx) is open for %d seconds", esp_zb_get_pan_id(), *(uint8_t *)esp_zb_app_signal_get_params(p_sg_p));
            } else {
                ESP_LOGW(TAG, "Network(0x%04hx) closed, devices joining not allowed.", esp_zb_get_pan_id());
            }
        }
        break;

    default:
        ESP_LOGI(TAG, "ZDO signal: %s (0x%x), status: %s", esp_zb_zdo_signal_to_string(sig_type), sig_type,
                 esp_err_to_name(err_status));
        break;
    }
}

static void esp_zb_task(void *pvParameters)
{
    /* initialize Zigbee stack */
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZC_CONFIG();
    esp_zb_init(&zb_nwk_cfg);
    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);

    /* create endpoint and clusters */
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();

    esp_zb_endpoint_config_t endpoint_config = {
        .endpoint = ESP_ZB_GATEWAY_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_REMOTE_CONTROL_DEVICE_ID,
        .app_device_version = 0,
    };

    esp_zb_attribute_list_t *basic_cluster = esp_zb_basic_cluster_create(NULL);
    esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, "\x09ESPRESSIF");
    esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, "\x07" CONFIG_IDF_TARGET);

    esp_zb_cluster_list_add_basic_cluster(cluster_list, basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    esp_zb_cluster_list_add_identify_cluster(cluster_list, esp_zb_identify_cluster_create(NULL), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    esp_zb_ep_list_add_gateway_ep(ep_list, cluster_list, endpoint_config);
    esp_zb_device_register(ep_list);
    esp_zb_core_action_handler_register(zigbee_action_handler);

    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();

    vTaskDelete(NULL);
}

static void uart_receive_task(void *pvParameters)
{
    uint8_t buffer[256];
    char line[256];
    size_t line_len = 0;

    ESP_LOGI(TAG_UART, "UART receive task started");

    while (true) {
        int len = uart_read_bytes(UART_PORT_NUM, buffer, sizeof(buffer), pdMS_TO_TICKS(100));

        if (len <= 0) continue;

        for (int i = 0; i < len; i++) {
            char c = (char)buffer[i];

            if (c == '\n') {
                line[line_len] = '\0';

                if (line_len > 0) {
                    ESP_LOGI(TAG_UART, "Recebido do S3: %s", line);

                    cJSON *root = cJSON_Parse(line);
                    cJSON *type = root != NULL ? cJSON_GetObjectItem(root, "type") : NULL;
                    cJSON *request_id = root != NULL ? cJSON_GetObjectItem(root, "request_id") : NULL;
                    cJSON *device = root != NULL ? cJSON_GetObjectItem(root, "device") : NULL;
                    cJSON *action = root != NULL ? cJSON_GetObjectItem(root, "action") : NULL;

                    if (!cJSON_IsObject(root) || !cJSON_IsString(type) ||
                        strcmp(type->valuestring, "command") != 0 ||
                        !cJSON_IsNumber(request_id) || !cJSON_IsString(device) ||
                        !cJSON_IsString(action)) {
                        uart_send_response(cJSON_IsNumber(request_id) ? request_id->valueint : 0,
                                           cJSON_IsString(device) ? device->valuestring : "",
                                           "error", "invalid_command");
                    } else if (strcmp(device->valuestring, "luz_sala") != 0) {
                        uart_send_response(request_id->valueint, device->valuestring,
                                           "error", "device_not_found");
                    } else if (!zigbee_send_on_off(action->valuestring)) {
                        uart_send_response(request_id->valueint, device->valuestring,
                                           "error", "unsupported_action");
                    } else {
                        ESP_LOGI(TAG, "Zigbee command sent: device=%s action=%s",
                                 device->valuestring, action->valuestring);
                        uart_send_response(request_id->valueint, device->valuestring,
                                           "success", NULL);
                    }
                    cJSON_Delete(root);
                }

                line_len = 0;
            } else if (line_len < sizeof(line) - 1) {
                if (c != '\r') {
                    line[line_len++] = c;
                }
            }
        }
    }
}

void app_main(void)
{
    /* Initialize platform */
    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };

    ESP_ERROR_CHECK(esp_zb_platform_config(&config));

    /* Initialize UART */
    const uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, 1024, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, UART_TX_GPIO, UART_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_LOGI(TAG_UART, "UART initialized: TX=%d RX=%d", UART_TX_GPIO, UART_RX_GPIO);
    uart_tx_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(uart_tx_mutex != NULL ? ESP_OK : ESP_ERR_NO_MEM);

    /* Start UART receive task */
    xTaskCreate(uart_receive_task, "uart_receive_task", 4096, NULL, 5, NULL);

    /* Start Zigbee task */
    xTaskCreate(esp_zb_task, "esp_zb_task", 8192, NULL, 5, NULL);

    ESP_LOGI(TAG, "hub_c6 started");
}
