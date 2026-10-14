/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* WiFi softAP & station Example - Modificado para hub_s3 do TCC */

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "driver/uart.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "nvs.h"

/* STA Configuration */
#define EXAMPLE_ESP_WIFI_STA_SSID "EQUIPE K"
#define EXAMPLE_ESP_WIFI_STA_PASSWD "12345678"
#define EXAMPLE_ESP_MAXIMUM_RETRY 5

/* AP Configuration */
#define EXAMPLE_ESP_WIFI_AP_SSID "MeuHub-IoT"
#define EXAMPLE_ESP_WIFI_AP_PASSWD "12345678"
#define EXAMPLE_ESP_WIFI_CHANNEL 1
#define EXAMPLE_MAX_STA_CONN 4

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

/* UART Configuration */
#define UART_PORT_NUM     UART_NUM_1
#define UART_TX_GPIO      17
#define UART_RX_GPIO      18
#define UART_BAUD_RATE    115200
#define MAX_AUTOMATION_RULES 8
#define HISTORY_CAPACITY 16

static const char *TAG_AP = "WiFi SoftAP";
static const char *TAG_STA = "WiFi Sta";
static const char *TAG_UART = "UART";
static const char *TAG_HTTP = "HTTP";

static int s_retry_num = 0;
static int s_next_request_id = 1;
static EventGroupHandle_t s_wifi_event_group;

typedef struct {
    bool discovered;
    bool online;
    bool state_known;
    bool state;
    uint16_t short_address;
    uint8_t endpoint;
    char ieee_address[24];
} zigbee_device_registry_t;

static zigbee_device_registry_t s_luz_sala = {0};
static SemaphoreHandle_t s_registry_mutex;
static uint32_t s_event_count;
static uint32_t s_rule_match_count;
static uint32_t s_automation_command_count;
static uint32_t s_uart_response_count;
static uint32_t s_uart_error_count;
static int64_t s_last_event_us;
static bool s_rule_execution_active;

typedef struct {
    uint32_t id;
    bool configured;
    bool enabled;
    char trigger_device[32];
    char trigger_event[32];
    bool trigger_value;
    char action_device[32];
    char action[8];
} automation_rule_t;

typedef struct {
    int64_t timestamp_us;
    char type[12];
    char device[32];
    char name[32];
    bool value;
    bool has_value;
} automation_history_entry_t;

static automation_rule_t s_rules[MAX_AUTOMATION_RULES] = {0};
static automation_history_entry_t s_history[HISTORY_CAPACITY] = {0};
static size_t s_history_count;
static size_t s_history_next;
static SemaphoreHandle_t s_history_mutex;

static void history_add(const char *type, const char *device, const char *name,
                        bool has_value, bool value)
{
    if (s_history_mutex == NULL ||
        xSemaphoreTake(s_history_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    automation_history_entry_t *entry = &s_history[s_history_next];
    entry->timestamp_us = esp_timer_get_time();
    snprintf(entry->type, sizeof(entry->type), "%s", type);
    snprintf(entry->device, sizeof(entry->device), "%s", device);
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    entry->has_value = has_value;
    entry->value = value;
    s_history_next = (s_history_next + 1) % HISTORY_CAPACITY;
    if (s_history_count < HISTORY_CAPACITY) {
        s_history_count++;
    }
    xSemaphoreGive(s_history_mutex);
}

static cJSON *rule_to_json(const automation_rule_t *rule)
{
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        return NULL;
    }
    cJSON_AddNumberToObject(json, "id", rule->id);
    cJSON_AddBoolToObject(json, "configured", rule->configured);
    cJSON_AddBoolToObject(json, "enabled", rule->enabled);
    cJSON_AddStringToObject(json, "trigger_device", rule->trigger_device);
    cJSON_AddStringToObject(json, "trigger_event", rule->trigger_event);
    cJSON_AddBoolToObject(json, "trigger_value", rule->trigger_value);
    cJSON_AddStringToObject(json, "action_device", rule->action_device);
    cJSON_AddStringToObject(json, "action", rule->action);
    return json;
}

static bool rule_from_json(cJSON *json, automation_rule_t *rule, uint32_t default_id)
{
    cJSON *trigger_device = cJSON_GetObjectItem(json, "trigger_device");
    cJSON *trigger_event = cJSON_GetObjectItem(json, "trigger_event");
    cJSON *trigger_value = cJSON_GetObjectItem(json, "trigger_value");
    cJSON *action_device = cJSON_GetObjectItem(json, "action_device");
    cJSON *action = cJSON_GetObjectItem(json, "action");
    if (!cJSON_IsString(trigger_device) || !cJSON_IsString(trigger_event) ||
        !cJSON_IsBool(trigger_value) || !cJSON_IsString(action_device) ||
        !cJSON_IsString(action) || strlen(trigger_device->valuestring) >= sizeof(rule->trigger_device) ||
        strlen(trigger_event->valuestring) >= sizeof(rule->trigger_event) ||
        strlen(action_device->valuestring) >= sizeof(rule->action_device) ||
        strlen(action->valuestring) >= sizeof(rule->action) ||
        (strcmp(action->valuestring, "on") != 0 && strcmp(action->valuestring, "off") != 0)) {
        return false;
    }
    cJSON *id = cJSON_GetObjectItem(json, "id");
    rule->id = cJSON_IsNumber(id) && id->valueint > 0 ? (uint32_t)id->valueint : default_id;
    rule->configured = true;
    rule->enabled = true;
    cJSON *enabled = cJSON_GetObjectItem(json, "enabled");
    if (cJSON_IsBool(enabled)) {
        rule->enabled = cJSON_IsTrue(enabled);
    }
    snprintf(rule->trigger_device, sizeof(rule->trigger_device), "%s", trigger_device->valuestring);
    snprintf(rule->trigger_event, sizeof(rule->trigger_event), "%s", trigger_event->valuestring);
    rule->trigger_value = cJSON_IsTrue(trigger_value);
    snprintf(rule->action_device, sizeof(rule->action_device), "%s", action_device->valuestring);
    snprintf(rule->action, sizeof(rule->action), "%s", action->valuestring);
    return true;
}

static void rules_save(void)
{
    nvs_handle_t handle;
    if (nvs_open("automation", NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGE(TAG_HTTP, "Nao foi possivel abrir NVS de automacao");
        return;
    }
    esp_err_t err = ESP_OK;
    for (size_t i = 0; i < MAX_AUTOMATION_RULES; i++) {
        char key[12];
        snprintf(key, sizeof(key), "rule_%u", (unsigned)i);
        if (!s_rules[i].configured) {
            nvs_erase_key(handle, key);
            continue;
        }
        cJSON *json = rule_to_json(&s_rules[i]);
        char *serialized = json != NULL ? cJSON_PrintUnformatted(json) : NULL;
        cJSON_Delete(json);
        if (serialized == NULL || strlen(serialized) >= 384 ||
            nvs_set_str(handle, key, serialized) != ESP_OK) {
            err = ESP_FAIL;
        }
        free(serialized);
    }
    if (nvs_commit(handle) != ESP_OK) {
        err = ESP_FAIL;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG_HTTP, "Falha ao salvar regra");
    }
    nvs_close(handle);
}

static void rules_load(void)
{
    nvs_handle_t handle;
    if (nvs_open("automation", NVS_READONLY, &handle) != ESP_OK) {
        return;
    }

    for (size_t i = 0; i < MAX_AUTOMATION_RULES; i++) {
        char key[12];
        snprintf(key, sizeof(key), "rule_%u", (unsigned)i);
        size_t size = 0;
        if (nvs_get_str(handle, key, NULL, &size) == ESP_OK && size <= 384) {
            char serialized[384];
            if (nvs_get_str(handle, key, serialized, &size) == ESP_OK) {
            cJSON *rule = cJSON_Parse(serialized);
            if (rule != NULL) {
                rule_from_json(rule, &s_rules[i], (uint32_t)i + 1);
                cJSON_Delete(rule);
            }
        }
        }
    }
    nvs_close(handle);
}

static void send_automation_command(const char *device, const char *action)
{
    char message[192];
    int request_id = s_next_request_id++;
    int length = snprintf(message, sizeof(message),
                           "{\"type\":\"command\",\"request_id\":%d,\"device\":\"%s\",\"action\":\"%s\"}",
                           request_id, device, action);
    if (length < 0 || (size_t)length >= sizeof(message)) {
        ESP_LOGE(TAG_HTTP, "Acao de automacao muito grande");
        return;
    }
    uart_write_bytes(UART_PORT_NUM, message, length);
    uart_write_bytes(UART_PORT_NUM, "\n", 1);
    s_automation_command_count++;
    history_add("command", device, action, false, false);
    ESP_LOGI(TAG_HTTP, "Regra executada: %s -> %s", device, action);
}

static bool automation_process_event(const char *device, const char *event, bool value)
{
    s_event_count++;
    s_last_event_us = esp_timer_get_time();
    history_add("event", device, event, true, value);
    bool matched = false;
    if (s_rule_execution_active) {
        return false;
    }
    s_rule_execution_active = true;
    for (size_t i = 0; i < MAX_AUTOMATION_RULES; i++) {
        automation_rule_t *rule = &s_rules[i];
        if (rule->configured && rule->enabled &&
            strcmp(rule->trigger_device, device) == 0 &&
            strcmp(rule->trigger_event, event) == 0 &&
            rule->trigger_value == value) {
            s_rule_match_count++;
            send_automation_command(rule->action_device, rule->action);
            matched = true;
        }
    }
    s_rule_execution_active = false;
    return matched;
}

static void registry_load(void)
{
    nvs_handle_t handle;
    if (nvs_open("devices", NVS_READONLY, &handle) != ESP_OK) {
        return;
    }

    uint8_t discovered = 0;
    uint16_t short_address = 0;
    uint8_t endpoint = 0;
    uint8_t state_known = 0;
    uint8_t state = 0;
    size_t ieee_size = sizeof(s_luz_sala.ieee_address);

    esp_err_t err = nvs_get_u8(handle, "luz_discovered", &discovered);
    if (err == ESP_OK) {
        nvs_get_u16(handle, "luz_short", &short_address);
        nvs_get_u8(handle, "luz_endpoint", &endpoint);
        nvs_get_str(handle, "luz_ieee", s_luz_sala.ieee_address, &ieee_size);
        nvs_get_u8(handle, "luz_state_known", &state_known);
        nvs_get_u8(handle, "luz_state", &state);
        s_luz_sala.discovered = discovered != 0;
        s_luz_sala.online = false;
        s_luz_sala.state_known = state_known != 0;
        s_luz_sala.state = state != 0;
        s_luz_sala.short_address = short_address;
        s_luz_sala.endpoint = endpoint;
    }
    nvs_close(handle);
}

static void registry_save(void)
{
    nvs_handle_t handle;
    if (nvs_open("devices", NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGE(TAG_UART, "Nao foi possivel abrir NVS de dispositivos");
        return;
    }

    esp_err_t err = nvs_set_u8(handle, "luz_discovered", s_luz_sala.discovered ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_set_u16(handle, "luz_short", s_luz_sala.short_address);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, "luz_endpoint", s_luz_sala.endpoint);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, "luz_ieee", s_luz_sala.ieee_address);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, "luz_state_known", s_luz_sala.state_known ? 1 : 0);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, "luz_state", s_luz_sala.state ? 1 : 0);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG_UART, "Falha ao salvar registro: %s", esp_err_to_name(err));
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *) event_data;
        ESP_LOGI(TAG_AP, "Station "MACSTR" joined, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *) event_data;
        ESP_LOGI(TAG_AP, "Station "MACSTR" left, AID=%d, reason:%d",
                 MAC2STR(event->mac), event->aid, event->reason);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG_STA, "Station started");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG_STA, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_netif_t *wifi_init_softap(void)
{
    esp_netif_t *esp_netif_ap = esp_netif_create_default_wifi_ap();

    wifi_config_t wifi_ap_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_AP_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_AP_SSID),
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .password = EXAMPLE_ESP_WIFI_AP_PASSWD,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    if (strlen(EXAMPLE_ESP_WIFI_AP_PASSWD) == 0) {
        wifi_ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));
    ESP_LOGI(TAG_AP, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_AP_SSID, EXAMPLE_ESP_WIFI_AP_PASSWD, EXAMPLE_ESP_WIFI_CHANNEL);

    return esp_netif_ap;
}

static esp_netif_t *wifi_init_sta(void)
{
    esp_netif_t *esp_netif_sta = esp_netif_create_default_wifi_sta();

    wifi_config_t wifi_sta_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_STA_SSID,
            .password = EXAMPLE_ESP_WIFI_STA_PASSWD,
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .failure_retry_cnt = EXAMPLE_ESP_MAXIMUM_RETRY,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config));
    ESP_LOGI(TAG_STA, "wifi_init_sta finished.");

    return esp_netif_sta;
}

static void uart_receive_task(void *pvParameters)
{
    uint8_t buffer[256];
    char line[256];
    size_t line_len = 0;
    bool line_overflow = false;

    ESP_LOGI(TAG_UART, "UART receive task started");

    while (true) {
        int len = uart_read_bytes(UART_PORT_NUM, buffer, sizeof(buffer), pdMS_TO_TICKS(100));

        if (len <= 0) continue;

        for (int i = 0; i < len; i++) {
            char c = (char)buffer[i];

            if (c == '\n') {
                if (line_overflow) {
                    ESP_LOGW(TAG_UART, "Mensagem UART descartada por excesso de tamanho");
                    line_len = 0;
                    line_overflow = false;
                    continue;
                }
                line[line_len] = '\0';

                if (line_len > 0) {
                    bool has_content = false;
                    for (size_t j = 0; j < line_len; ++j) {
                        if (line[j] != ' ' && line[j] != '\t') {
                            has_content = true;
                            break;
                        }
                    }
                    if (!has_content) {
                        line_len = 0;
                        continue;
                    }

                    ESP_LOGI(TAG_UART, "Recebido (%u bytes): %s",
                             (unsigned)line_len, line);

                    cJSON *message = cJSON_Parse(line);
                    if (message == NULL) {
                        const char *parse_error = cJSON_GetErrorPtr();
                        ESP_LOGW(TAG_UART, "Mensagem UART invalida; erro proximo de: %s",
                                 parse_error != NULL ? parse_error : "(desconhecido)");
                        ESP_LOG_BUFFER_HEXDUMP(TAG_UART, line, line_len, ESP_LOG_WARN);
                    } else {
                        cJSON *type = cJSON_GetObjectItem(message, "type");
                        if (!cJSON_IsString(type)) {
                            ESP_LOGW(TAG_UART, "Mensagem UART sem tipo");
                        } else if (strcmp(type->valuestring, "response") == 0) {
                            cJSON *status = cJSON_GetObjectItem(message, "status");
                            if (cJSON_IsString(status)) {
                                ESP_LOGI(TAG_UART, "Resposta do C6: status=%s", status->valuestring);
                                if (strcmp(status->valuestring, "success") == 0) {
                                    s_uart_response_count++;
                                } else {
                                    s_uart_error_count++;
                                }
                            }
                        } else if (strcmp(type->valuestring, "event") == 0) {
                            cJSON *event = cJSON_GetObjectItem(message, "event");
                            if (cJSON_IsString(event) &&
                                strcmp(event->valuestring, "device_discovered") == 0) {
                                cJSON *short_address = cJSON_GetObjectItem(message, "short_address");
                                cJSON *endpoint = cJSON_GetObjectItem(message, "endpoint");
                                cJSON *ieee_address = cJSON_GetObjectItem(message, "ieee_address");
                                if (cJSON_IsNumber(short_address) &&
                                    cJSON_IsNumber(endpoint) &&
                                    cJSON_IsString(ieee_address) &&
                                    xSemaphoreTake(s_registry_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                                    s_luz_sala.discovered = true;
                                    s_luz_sala.online = true;
                                    s_luz_sala.short_address = (uint16_t)short_address->valueint;
                                    s_luz_sala.endpoint = (uint8_t)endpoint->valueint;
                                    snprintf(s_luz_sala.ieee_address,
                                             sizeof(s_luz_sala.ieee_address), "%s",
                                             ieee_address->valuestring);
                                    registry_save();
                                    xSemaphoreGive(s_registry_mutex);
                                    ESP_LOGI(TAG_UART, "Dispositivo luz_sala atualizado: short=0x%04x endpoint=%u ieee=%s",
                                             s_luz_sala.short_address, s_luz_sala.endpoint,
                                             s_luz_sala.ieee_address);
                                } else {
                                    ESP_LOGW(TAG_UART, "Evento device_discovered invalido");
                                }
                            } else if (cJSON_IsString(event) &&
                                       strcmp(event->valuestring, "state_changed") == 0) {
                                cJSON *value = cJSON_GetObjectItem(message, "value");
                                if (cJSON_IsBool(value) &&
                                    xSemaphoreTake(s_registry_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                                    s_luz_sala.state_known = true;
                                    s_luz_sala.state = cJSON_IsTrue(value);
                                    s_luz_sala.online = true;
                                    registry_save();
                                    xSemaphoreGive(s_registry_mutex);
                                    ESP_LOGI(TAG_UART, "Estado luz_sala: %s",
                                             s_luz_sala.state ? "on" : "off");
                                    automation_process_event("luz_sala", "state_changed", s_luz_sala.state);
                                } else {
                                    ESP_LOGW(TAG_UART, "Evento state_changed invalido");
                                }
                            } else {
                                ESP_LOGI(TAG_UART, "Evento recebido do C6");
                            }
                        } else {
                            ESP_LOGW(TAG_UART, "Tipo UART inesperado: %s", type->valuestring);
                        }
                        cJSON_Delete(message);
                    }
                }
                line_len = 0;
                line_overflow = false;
            } else if (line_len < sizeof(line) - 1) {
                if (c == '\r') {
                    continue;
                }
                if (line_len == 0 && c != '{') {
                    continue;
                }
                if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7e) {
                    ESP_LOGW(TAG_UART, "Byte invalido no quadro UART: 0x%02x",
                             (unsigned char)c);
                    line_len = 0;
                    line_overflow = false;
                    continue;
                }
                if (c != '\r') {
                    line[line_len++] = c;
                }
            } else {
                line_overflow = true;
            }
        }
    }
}

// HTTP GET /api/devices
static esp_err_t api_devices_get_handler(httpd_req_t *req)
{
    cJSON *devices = cJSON_CreateArray();
    if (devices == NULL || xSemaphoreTake(s_registry_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        cJSON_Delete(devices);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Registro indisponivel");
        return ESP_FAIL;
    }

    if (s_luz_sala.discovered) {
        cJSON *device = cJSON_CreateObject();
        cJSON_AddStringToObject(device, "id", "luz_sala");
        cJSON_AddStringToObject(device, "type", "on_off");
        cJSON_AddBoolToObject(device, "online", s_luz_sala.online);
        if (s_luz_sala.state_known) {
            cJSON_AddBoolToObject(device, "state", s_luz_sala.state);
        }
        cJSON_AddNumberToObject(device, "short_address", s_luz_sala.short_address);
        cJSON_AddNumberToObject(device, "endpoint", s_luz_sala.endpoint);
        cJSON_AddStringToObject(device, "ieee_address", s_luz_sala.ieee_address);
        cJSON_AddItemToArray(devices, device);
    }
    xSemaphoreGive(s_registry_mutex);

    char *json_response = cJSON_PrintUnformatted(devices);
    cJSON_Delete(devices);
    if (json_response == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Falha ao serializar dispositivos");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_response);
    free(json_response);
    return ESP_OK;
}

static esp_err_t api_rules_get_handler(httpd_req_t *req)
{
    cJSON *rules = cJSON_CreateArray();
    if (rules == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Falha ao criar resposta");
        return ESP_FAIL;
    }
    for (size_t i = 0; i < MAX_AUTOMATION_RULES; i++) {
        if (s_rules[i].configured) {
            cJSON *rule = rule_to_json(&s_rules[i]);
            if (rule == NULL) {
                cJSON_Delete(rules);
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Falha ao criar regra");
                return ESP_FAIL;
            }
            cJSON_AddItemToArray(rules, rule);
        }
    }
    char *response = cJSON_PrintUnformatted(rules);
    cJSON_Delete(rules);
    if (response == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Falha ao serializar regra");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    free(response);
    return ESP_OK;
}

static esp_err_t api_rules_post_handler(httpd_req_t *req)
{
    char body[256];
    if (req->content_len == 0 || req->content_len >= sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body de regra invalido");
        return ESP_FAIL;
    }
    size_t received = 0;
    while (received < req->content_len) {
        int length = httpd_req_recv(req, body + received, req->content_len - received);
        if (length == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (length <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Falha ao receber regra");
            return ESP_FAIL;
        }
        received += (size_t)length;
    }
    body[received] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON de regra invalido");
        return ESP_FAIL;
    }
    cJSON *id = cJSON_GetObjectItem(root, "id");
    size_t slot = MAX_AUTOMATION_RULES;
    if (cJSON_IsNumber(id) && id->valueint > 0) {
        for (size_t i = 0; i < MAX_AUTOMATION_RULES; i++) {
            if (s_rules[i].configured && s_rules[i].id == (uint32_t)id->valueint) {
                slot = i;
                break;
            }
        }
    } else {
        for (size_t i = 0; i < MAX_AUTOMATION_RULES; i++) {
            if (!s_rules[i].configured) {
                slot = i;
                break;
            }
        }
    }
    if (slot == MAX_AUTOMATION_RULES || !rule_from_json(root, &s_rules[slot], (uint32_t)slot + 1)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            slot == MAX_AUTOMATION_RULES ? "Limite de regras atingido" : "Campos de regra invalidos");
        return ESP_FAIL;
    }
    uint32_t saved_id = s_rules[slot].id;
    cJSON_Delete(root);
    rules_save();
    httpd_resp_set_type(req, "application/json");
    char response[96];
    snprintf(response, sizeof(response), "{\"status\":\"rule_saved\",\"id\":%u}", (unsigned)saved_id);
    httpd_resp_sendstr(req, response);
    return ESP_OK;
}

static bool rule_id_from_uri(httpd_req_t *req, uint32_t *id)
{
    const char *prefix = "/api/rules/";
    if (strncmp(req->uri, prefix, strlen(prefix)) != 0) {
        return false;
    }
    char *end = NULL;
    unsigned long parsed = strtoul(req->uri + strlen(prefix), &end, 10);
    if (end == req->uri + strlen(prefix) || *end != '\0' || parsed == 0 ||
        parsed > UINT32_MAX) {
        return false;
    }
    *id = (uint32_t)parsed;
    return true;
}

static esp_err_t api_rule_put_handler(httpd_req_t *req)
{
    uint32_t id;
    if (!rule_id_from_uri(req, &id)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ID de regra invalido");
        return ESP_FAIL;
    }
    char body[256];
    if (req->content_len == 0 || req->content_len >= sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body de regra invalido");
        return ESP_FAIL;
    }
    size_t received = 0;
    while (received < req->content_len) {
        int length = httpd_req_recv(req, body + received, req->content_len - received);
        if (length == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (length <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Falha ao receber regra");
            return ESP_FAIL;
        }
        received += (size_t)length;
    }
    body[received] = '\0';
    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON de regra invalido");
        return ESP_FAIL;
    }
    size_t slot = MAX_AUTOMATION_RULES;
    for (size_t i = 0; i < MAX_AUTOMATION_RULES; i++) {
        if (s_rules[i].configured && s_rules[i].id == id) {
            slot = i;
            break;
        }
    }
    automation_rule_t replacement = {0};
    bool valid = slot < MAX_AUTOMATION_RULES && rule_from_json(root, &replacement, id);
    cJSON_Delete(root);
    if (!valid) {
        httpd_resp_send_err(req, slot == MAX_AUTOMATION_RULES ? HTTPD_404_NOT_FOUND : HTTPD_400_BAD_REQUEST,
                            slot == MAX_AUTOMATION_RULES ? "Regra nao encontrada" : "Campos de regra invalidos");
        return ESP_FAIL;
    }
    s_rules[slot] = replacement;
    rules_save();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"rule_updated\"}");
    return ESP_OK;
}

static esp_err_t api_rule_delete_handler(httpd_req_t *req)
{
    uint32_t id;
    if (!rule_id_from_uri(req, &id)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ID de regra invalido");
        return ESP_FAIL;
    }
    for (size_t i = 0; i < MAX_AUTOMATION_RULES; i++) {
        if (s_rules[i].configured && s_rules[i].id == id) {
            memset(&s_rules[i], 0, sizeof(s_rules[i]));
            rules_save();
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, "{\"status\":\"rule_deleted\"}");
            return ESP_OK;
        }
    }
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Regra nao encontrada");
    return ESP_FAIL;
}

static esp_err_t api_test_event_post_handler(httpd_req_t *req)
{
    char body[192];
    if (req->content_len == 0 || req->content_len >= sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body de evento invalido");
        return ESP_FAIL;
    }

    size_t received = 0;
    while (received < req->content_len) {
        int length = httpd_req_recv(req, body + received, req->content_len - received);
        if (length == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (length <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Falha ao receber evento");
            return ESP_FAIL;
        }
        received += (size_t)length;
    }
    body[received] = '\0';

    cJSON *root = cJSON_Parse(body);
    cJSON *device = root != NULL ? cJSON_GetObjectItem(root, "device") : NULL;
    cJSON *event = root != NULL ? cJSON_GetObjectItem(root, "event") : NULL;
    cJSON *value = root != NULL ? cJSON_GetObjectItem(root, "value") : NULL;
    if (!cJSON_IsString(device) || !cJSON_IsString(event) || !cJSON_IsBool(value)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Campos de evento invalidos");
        return ESP_FAIL;
    }

    bool matched = automation_process_event(device->valuestring, event->valuestring, cJSON_IsTrue(value));
    cJSON_Delete(root);

    char response[96];
    snprintf(response, sizeof(response),
             "{\"status\":\"event_processed\",\"source\":\"test\",\"rule_matched\":%s}",
             matched ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    return ESP_OK;
}

static esp_err_t api_status_get_handler(httpd_req_t *req)
{
    cJSON *status = cJSON_CreateObject();
    if (status == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Falha ao criar status");
        return ESP_FAIL;
    }

    cJSON_AddStringToObject(status, "status", "ok");
    cJSON_AddNumberToObject(status, "uptime_seconds",
                            (double)(esp_timer_get_time() / 1000000));
    cJSON_AddNumberToObject(status, "events_received", s_event_count);
    cJSON_AddNumberToObject(status, "rules_matched", s_rule_match_count);
    cJSON_AddNumberToObject(status, "automation_commands", s_automation_command_count);
    cJSON_AddNumberToObject(status, "uart_success_responses", s_uart_response_count);
    cJSON_AddNumberToObject(status, "uart_error_responses", s_uart_error_count);
    cJSON_AddNumberToObject(status, "history_entries", s_history_count);
    size_t configured_rules = 0;
    size_t enabled_rules = 0;
    for (size_t i = 0; i < MAX_AUTOMATION_RULES; i++) {
        if (s_rules[i].configured) {
            configured_rules++;
            if (s_rules[i].enabled) {
                enabled_rules++;
            }
        }
    }
    cJSON_AddNumberToObject(status, "configured_rules", configured_rules);
    cJSON_AddNumberToObject(status, "enabled_rules", enabled_rules);
    cJSON_AddBoolToObject(status, "device_discovered", s_luz_sala.discovered);
    cJSON_AddBoolToObject(status, "device_online", s_luz_sala.online);

    char *response = cJSON_PrintUnformatted(status);
    cJSON_Delete(status);
    if (response == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Falha ao serializar status");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    free(response);
    return ESP_OK;
}

static esp_err_t api_history_get_handler(httpd_req_t *req)
{
    cJSON *history = cJSON_CreateArray();
    if (history == NULL || s_history_mutex == NULL ||
        xSemaphoreTake(s_history_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        cJSON_Delete(history);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Historico indisponivel");
        return ESP_FAIL;
    }
    size_t start = (s_history_count == HISTORY_CAPACITY) ? s_history_next : 0;
    for (size_t i = 0; i < s_history_count; i++) {
        size_t index = (start + i) % HISTORY_CAPACITY;
        automation_history_entry_t *entry = &s_history[index];
        cJSON *item = cJSON_CreateObject();
        if (item == NULL) {
            xSemaphoreGive(s_history_mutex);
            cJSON_Delete(history);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Falha ao criar historico");
            return ESP_FAIL;
        }
        cJSON_AddNumberToObject(item, "uptime_seconds",
                                (double)(entry->timestamp_us / 1000000));
        cJSON_AddStringToObject(item, "type", entry->type);
        cJSON_AddStringToObject(item, "device", entry->device);
        cJSON_AddStringToObject(item, "name", entry->name);
        if (entry->has_value) {
            cJSON_AddBoolToObject(item, "value", entry->value);
        }
        cJSON_AddItemToArray(history, item);
    }
    xSemaphoreGive(s_history_mutex);
    char *response = cJSON_PrintUnformatted(history);
    cJSON_Delete(history);
    if (response == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Falha ao serializar historico");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    free(response);
    return ESP_OK;
}

// HTTP POST /api/devices/<id>/command
static esp_err_t api_device_command_post_handler(httpd_req_t *req)
{
    char buf[256];
    if (req->content_len == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body vazio");
        return ESP_FAIL;
    }
    if (req->content_len >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "Body muito grande");
        return ESP_FAIL;
    }

    size_t received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, buf + received, req->content_len - received);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (ret <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Falha ao receber body");
            return ESP_FAIL;
        }
        received += (size_t)ret;
    }

    buf[received] = '\0';
    ESP_LOGI(TAG_HTTP, "Body recebido (%u bytes): <%s>", (unsigned)received, buf);

    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) {
        const char *parse_error = cJSON_GetErrorPtr();
        ESP_LOGW(TAG_HTTP, "JSON invalido; erro proximo de: <%s>",
                 parse_error != NULL ? parse_error : "(desconhecido)");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON invalido");
        return ESP_FAIL;
    }

    cJSON *action = cJSON_GetObjectItem(root, "action");
    if (!cJSON_IsString(action)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Campo 'action' ausente");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG_HTTP, "Comando: action=%s", action->valuestring);

    const char *device_prefix = "/api/devices/";
    const char *device_start = strstr(req->uri, device_prefix);
    const char *command_suffix = device_start != NULL ? strstr(device_start + strlen(device_prefix), "/command") : NULL;
    if (device_start == NULL || command_suffix == NULL ||
        command_suffix == device_start + strlen(device_prefix)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Dispositivo ausente na URL");
        return ESP_FAIL;
    }

    char device_id[64];
    size_t device_len = (size_t)(command_suffix - (device_start + strlen(device_prefix)));
    if (device_len >= sizeof(device_id)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ID de dispositivo muito grande");
        return ESP_FAIL;
    }
    memcpy(device_id, device_start + strlen(device_prefix), device_len);
    device_id[device_len] = '\0';

    if (strcmp(action->valuestring, "on") != 0 &&
        strcmp(action->valuestring, "off") != 0) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Acao deve ser on ou off");
        return ESP_FAIL;
    }

    // Envia comando via UART para C6
    char uart_msg[256];
    int request_id = s_next_request_id++;
    int written = snprintf(uart_msg, sizeof(uart_msg),
                           "{\"type\":\"command\",\"request_id\":%d,\"device\":\"%s\",\"action\":\"%s\"}",
                           request_id, device_id, action->valuestring);
    if (written < 0 || (size_t)written >= sizeof(uart_msg)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Comando muito grande");
        return ESP_FAIL;
    }
    uart_write_bytes(UART_PORT_NUM, uart_msg, strlen(uart_msg));
    uart_write_bytes(UART_PORT_NUM, "\n", 1);
    history_add("command", device_id, action->valuestring, false, false);

    cJSON_Delete(root);

    char json_response[128];
    snprintf(json_response, sizeof(json_response),
             "{\"status\":\"command_sent\",\"request_id\":%d,\"device\":\"%s\"}",
             request_id, device_id);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_response);
    return ESP_OK;
}

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize event group
    s_wifi_event_group = xEventGroupCreate();
    s_registry_mutex = xSemaphoreCreateMutex();
    s_history_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(s_registry_mutex != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(s_history_mutex != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    registry_load();
    rules_load();

    // Register Event handler
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    // Initialize AP
    ESP_LOGI(TAG_AP, "ESP_WIFI_MODE_AP");
    wifi_init_softap();

    // Initialize STA
    ESP_LOGI(TAG_STA, "ESP_WIFI_MODE_STA");
    esp_netif_t *esp_netif_sta = wifi_init_sta();

    // Start WiFi
    ESP_ERROR_CHECK(esp_wifi_start());

    // Wait for connection
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG_STA, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_STA_SSID, EXAMPLE_ESP_WIFI_STA_PASSWD);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG_STA, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_STA_SSID, EXAMPLE_ESP_WIFI_STA_PASSWD);
    } else {
        ESP_LOGE(TAG_STA, "UNEXPECTED EVENT");
    }

    // Set default netif
    esp_netif_set_default_netif(esp_netif_sta);

    // Initialize UART
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

    // Start UART receive task
    xTaskCreate(uart_receive_task, "uart_receive_task", 4096, NULL, 5, NULL);

    // Start HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.max_uri_handlers = 12;
    config.uri_match_fn = httpd_uri_match_wildcard;

    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &config));

    httpd_uri_t uri_devices_get = {
        .uri = "/api/devices",
        .method = HTTP_GET,
        .handler = api_devices_get_handler,
        .user_ctx = NULL
    };

    httpd_uri_t uri_device_command_post = {
        .uri = "/api/devices/*",
        .method = HTTP_POST,
        .handler = api_device_command_post_handler,
        .user_ctx = NULL
    };

    httpd_uri_t uri_rules_get = {
        .uri = "/api/rules",
        .method = HTTP_GET,
        .handler = api_rules_get_handler,
        .user_ctx = NULL
    };

    httpd_uri_t uri_rules_post = {
        .uri = "/api/rules",
        .method = HTTP_POST,
        .handler = api_rules_post_handler,
        .user_ctx = NULL
    };

    httpd_uri_t uri_rule_put = {
        .uri = "/api/rules/*",
        .method = HTTP_PUT,
        .handler = api_rule_put_handler,
        .user_ctx = NULL
    };

    httpd_uri_t uri_rule_delete = {
        .uri = "/api/rules/*",
        .method = HTTP_DELETE,
        .handler = api_rule_delete_handler,
        .user_ctx = NULL
    };

    httpd_uri_t uri_test_event_post = {
        .uri = "/api/test/events",
        .method = HTTP_POST,
        .handler = api_test_event_post_handler,
        .user_ctx = NULL
    };

    httpd_uri_t uri_status_get = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = api_status_get_handler,
        .user_ctx = NULL
    };

    httpd_uri_t uri_history_get = {
        .uri = "/api/history",
        .method = HTTP_GET,
        .handler = api_history_get_handler,
        .user_ctx = NULL
    };

    httpd_register_uri_handler(server, &uri_devices_get);
    httpd_register_uri_handler(server, &uri_device_command_post);
    httpd_register_uri_handler(server, &uri_rules_get);
    httpd_register_uri_handler(server, &uri_rules_post);
    httpd_register_uri_handler(server, &uri_rule_put);
    httpd_register_uri_handler(server, &uri_rule_delete);
    httpd_register_uri_handler(server, &uri_test_event_post);
    httpd_register_uri_handler(server, &uri_status_get);
    httpd_register_uri_handler(server, &uri_history_get);

    ESP_LOGI(TAG_HTTP, "HTTP server started on port %d", config.server_port);
}
