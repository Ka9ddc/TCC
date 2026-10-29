/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* WiFi softAP & station Example - Modificado para hub_s3 do TCC */

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
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
#define WIFI_CONFIG_MAX_LENGTH 63
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
#define WIFI_SENSOR_TIMEOUT_SECONDS 30
#define LUMINOSITY_MIN_DELTA 5.0
#define WIFI_SENSOR_MIN_INTERVAL_SECONDS 2
#define MAX_RULE_ACTIONS 4
#define ACTION_QUEUE_LENGTH 16
#define MAX_PENDING_COMMANDS 16
#define MAX_ZIGBEE_DEVICES 8
#define UART_PROTOCOL_VERSION 1
#define HUB_API_TOKEN_DEFAULT "TCC-Hub-2026-Local"
#define HUB_API_TOKEN_MAX_LENGTH 63

static const char *TAG_AP = "WiFi SoftAP";
static const char *TAG_STA = "WiFi Sta";
static const char *TAG_UART = "UART";
static const char *TAG_HTTP = "HTTP";

static int s_retry_num = 0;
static int s_next_request_id = 1;
static EventGroupHandle_t s_wifi_event_group;
static char s_api_token[HUB_API_TOKEN_MAX_LENGTH + 1] = HUB_API_TOKEN_DEFAULT;
static char s_sta_ssid[WIFI_CONFIG_MAX_LENGTH + 1] = EXAMPLE_ESP_WIFI_STA_SSID;
static char s_sta_password[WIFI_CONFIG_MAX_LENGTH + 1] = EXAMPLE_ESP_WIFI_STA_PASSWD;
static char s_ap_ssid[WIFI_CONFIG_MAX_LENGTH + 1] = EXAMPLE_ESP_WIFI_AP_SSID;
static char s_ap_password[WIFI_CONFIG_MAX_LENGTH + 1] = EXAMPLE_ESP_WIFI_AP_PASSWD;

typedef struct {
    bool discovered;
    bool online;
    bool state_known;
    bool state;
    char id[32];
    uint16_t short_address;
    uint8_t endpoint;
    char ieee_address[24];
} zigbee_device_registry_t;

static zigbee_device_registry_t s_zigbee_devices[MAX_ZIGBEE_DEVICES] = {0};

static zigbee_device_registry_t *find_zigbee_device(const char *id)
{
    for (size_t i = 0; i < MAX_ZIGBEE_DEVICES; ++i) {
        if (s_zigbee_devices[i].id[0] != '\0' &&
            strcmp(s_zigbee_devices[i].id, id) == 0) {
            return &s_zigbee_devices[i];
        }
    }
    return NULL;
}

static zigbee_device_registry_t *get_or_create_zigbee_device(const char *id)
{
    zigbee_device_registry_t *device = find_zigbee_device(id);
    if (device != NULL) {
        return device;
    }
    for (size_t i = 0; i < MAX_ZIGBEE_DEVICES; ++i) {
        if (!s_zigbee_devices[i].discovered && s_zigbee_devices[i].id[0] == '\0') {
            snprintf(s_zigbee_devices[i].id, sizeof(s_zigbee_devices[i].id), "%s", id);
            return &s_zigbee_devices[i];
        }
    }
    return NULL;
}

typedef struct {
    bool discovered;
    bool online;
    bool value_known;
    double value;
    int64_t last_event_us;
    char device_id[32];
    char event_name[32];
    char unit[16];
} wifi_sensor_registry_t;

static wifi_sensor_registry_t s_wifi_sensor = {0};
static SemaphoreHandle_t s_registry_mutex;
static uint32_t s_event_count;
static uint32_t s_rule_match_count;
static uint32_t s_automation_command_count;
static uint32_t s_uart_response_count;
static uint32_t s_uart_error_count;
static uint32_t s_uart_invalid_message_count;
static uint32_t s_uart_unknown_response_count;
static uint32_t s_uart_duplicate_response_count;
static int64_t s_last_event_us;
static bool s_rule_execution_active;

static void api_token_load(void)
{
    nvs_handle_t handle;
    if (nvs_open("config", NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    size_t length = sizeof(s_api_token);
    if (nvs_get_str(handle, "api_token", s_api_token, &length) != ESP_OK ||
        length <= 1 || length > sizeof(s_api_token)) {
        snprintf(s_api_token, sizeof(s_api_token), "%s", HUB_API_TOKEN_DEFAULT);
    }
    nvs_close(handle);
}

static esp_err_t api_token_save(const char *token)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("config", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(handle, "api_token", token);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static void wifi_config_load(void)
{
    nvs_handle_t handle;
    if (nvs_open("config", NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    size_t size = sizeof(s_sta_ssid);
    nvs_get_str(handle, "sta_ssid", s_sta_ssid, &size);
    size = sizeof(s_sta_password);
    nvs_get_str(handle, "sta_pass", s_sta_password, &size);
    size = sizeof(s_ap_ssid);
    nvs_get_str(handle, "ap_ssid", s_ap_ssid, &size);
    size = sizeof(s_ap_password);
    nvs_get_str(handle, "ap_pass", s_ap_password, &size);
    nvs_close(handle);
}

static esp_err_t wifi_config_save(const char *sta_ssid, const char *sta_password,
                                  const char *ap_ssid, const char *ap_password)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("config", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(handle, "sta_ssid", sta_ssid);
    if (err == ESP_OK) err = nvs_set_str(handle, "sta_pass", sta_password);
    if (err == ESP_OK) err = nvs_set_str(handle, "ap_ssid", ap_ssid);
    if (err == ESP_OK) err = nvs_set_str(handle, "ap_pass", ap_password);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static bool api_request_is_authenticated(httpd_req_t *req)
{
    size_t header_length = httpd_req_get_hdr_value_len(req, "Authorization");
    const char prefix[] = "Bearer ";
    const size_t prefix_length = sizeof(prefix) - 1;
    const size_t token_length = strlen(s_api_token);
    char authorization[sizeof(prefix) + HUB_API_TOKEN_MAX_LENGTH + 1];

    if (header_length != prefix_length + token_length ||
        header_length >= sizeof(authorization) ||
        httpd_req_get_hdr_value_str(req, "Authorization", authorization,
                                    sizeof(authorization)) != ESP_OK ||
        memcmp(authorization, prefix, prefix_length) != 0 ||
        memcmp(authorization + prefix_length, s_api_token, token_length) != 0) {
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Bearer");
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Token ausente ou invalido");
        return false;
    }
    return true;
}

static esp_err_t api_root_get_handler(httpd_req_t *req)
{
    static const char *html_page =
        "<!doctype html><html lang=\"pt-BR\"><head>"
        "<meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>MeuHub IoT</title>"
        "<style>"
        "body{font-family:Arial,Helvetica,sans-serif;background:#0f172a;color:#e2e8f0;margin:0;padding:32px}"
        "main{max-width:960px;margin:0 auto}"
        ".card{background:#111827;border:1px solid #334155;border-radius:12px;padding:20px;margin-bottom:18px;box-shadow:0 8px 18px rgba(15,23,42,.25)}"
        "h1{margin-top:0}"
        ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:16px}"
        ".stat{background:#0b1120;border:1px solid #334155;border-radius:10px;padding:16px}"
        ".label{font-size:12px;color:#94a3b8;text-transform:uppercase;letter-spacing:.08em}"
        ".value{font-size:28px;margin-top:8px;font-weight:700}"
        "ul{margin:0;padding-left:20px}"
        "li{margin-bottom:8px}"
        "code{background:#020817;padding:4px 8px;border-radius:6px}"
        "</style></head><body><main>"
        "<div class=\"card\"><h1>MeuHub IoT</h1><p>Hub local de automação residencial em operação.</p>"
        "<div class=\"grid\"><div class=\"stat\"><div class=\"label\">Status</div><div class=\"value\" id=\"hub_status\">carregando...</div></div>"
        "<div class=\"stat\"><div class=\"label\">Dispositivos</div><div class=\"value\" id=\"device_count\">0</div></div>"
        "<div class=\"stat\"><div class=\"label\">Regras</div><div class=\"value\" id=\"rule_count\">0</div></div>"
        "</div></div>"
        "<div class=\"card\"><h2>Rede local</h2><ul><li>Hostname do hub: <code>hub-s3-local</code></li><li>Endpoint da API: <code>http://IP_DO_HUB</code></li><li>Configuração atual: Wi-Fi AP + STA em operação.</li></ul></div>"
        "<div class=\"card\"><h2>Dispositivos cadastrados</h2><ul id=\"device_list\"><li>Carregando...</li></ul></div>"
        "</main><script>"
        "async function loadJSON(url){const res=await fetch(url); if(!res.ok) throw new Error(url+' -> '+res.status); return res.json();}"
        "async function refresh(){try{const status = await loadJSON('/api/status'); const devices = await loadJSON('/api/devices'); document.getElementById('hub_status').textContent = status.status || 'ok'; document.getElementById('device_count').textContent = Array.isArray(devices) ? devices.length : 0; document.getElementById('rule_count').textContent = status.configured_rules || 0; const deviceList = document.getElementById('device_list'); if(!Array.isArray(devices) || devices.length===0){ deviceList.innerHTML = '<li>Nenhum dispositivo encontrado.</li>'; return; } deviceList.innerHTML = devices.map(d => '<li><strong>'+ (d.id || 'device') + '</strong> (' + (d.protocol || 'unknown') + ') - ' + (d.online ? 'online' : 'offline') + (d.state!==undefined ? ' | state=' + d.state : '') + '</li>').join(''); }catch(err){document.getElementById('hub_status').textContent = 'erro'; document.getElementById('device_list').innerHTML = '<li>Falha ao carregar dados do hub.</li>'; console.error(err);}}"
        "refresh(); setInterval(refresh, 5000);"
        "</script></body></html>";

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req, (char *)html_page);
    return ESP_OK;
}

typedef enum {
    EVENT_VALUE_BOOL,
    EVENT_VALUE_NUMBER
} event_value_type_t;

typedef enum {
    COMMAND_WAITING,
    COMMAND_SUCCESS,
    COMMAND_ERROR,
    COMMAND_TIMEOUT
} command_result_t;

typedef struct {
    SemaphoreHandle_t semaphore;
    command_result_t result;
} command_waiter_t;

typedef struct {
    char device[32];
    char action[24];
    uint32_t delay_seconds;
    uint32_t duration_seconds;
    int request_id;
    command_waiter_t *waiter;
} automation_action_t;

typedef struct {
    bool active;
    int request_id;
    int64_t sent_at_us;
    char device[32];
    char action[24];
    command_waiter_t *waiter;
} pending_command_t;

typedef struct {
    uint32_t id;
    bool configured;
    bool enabled;
    char trigger_device[32];
    char trigger_event[32];
    event_value_type_t trigger_value_type;
    bool trigger_value_bool;
    double trigger_value_number;
    char operator[20];
    automation_action_t actions[MAX_RULE_ACTIONS];
    size_t action_count;
    bool condition_active;
} automation_rule_t;

typedef struct {
    int64_t timestamp_us;
    char type[12];
    char device[32];
    char name[32];
    bool value_is_number;
    double numeric_value;
    bool bool_value;
} automation_history_entry_t;

typedef struct {
    event_value_type_t type;
    bool bool_value;
    double numeric_value;
} event_value_t;

static automation_rule_t s_rules[MAX_AUTOMATION_RULES] = {0};
static automation_history_entry_t s_history[HISTORY_CAPACITY] = {0};
static size_t s_history_count;
static size_t s_history_next;
static SemaphoreHandle_t s_history_mutex;
static QueueHandle_t s_action_queue;
static pending_command_t s_pending_commands[MAX_PENDING_COMMANDS] = {0};
static SemaphoreHandle_t s_command_mutex;
static uint32_t s_command_timeout_count;
static int s_completed_request_ids[MAX_PENDING_COMMANDS] = {0};
static size_t s_completed_request_next;

static void wifi_sensor_save(void);

static void history_add(const char *type, const char *device, const char *name,
                        const event_value_t *value)
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
    entry->value_is_number = value != NULL && value->type == EVENT_VALUE_NUMBER;
    entry->numeric_value = value != NULL ? value->numeric_value : 0;
    entry->bool_value = value != NULL && value->bool_value;
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
    if (rule->trigger_value_type == EVENT_VALUE_NUMBER) {
        cJSON_AddNumberToObject(json, "trigger_value", rule->trigger_value_number);
    } else {
        cJSON_AddBoolToObject(json, "trigger_value", rule->trigger_value_bool);
    }
    cJSON_AddStringToObject(json, "operator", rule->operator);
    cJSON *actions = cJSON_AddArrayToObject(json, "actions");
    for (size_t i = 0; i < rule->action_count; i++) {
        cJSON *action = cJSON_CreateObject();
        cJSON_AddStringToObject(action, "device", rule->actions[i].device);
        cJSON_AddStringToObject(action, "action", rule->actions[i].action);
        if (rule->actions[i].delay_seconds > 0) {
            cJSON_AddNumberToObject(action, "delay_seconds", rule->actions[i].delay_seconds);
        }
        if (rule->actions[i].duration_seconds > 0) {
            cJSON_AddNumberToObject(action, "duration_seconds", rule->actions[i].duration_seconds);
        }
        cJSON_AddItemToArray(actions, action);
    }
    return json;
}

static bool operator_is_valid(const char *operator)
{
    return strcmp(operator, "equals") == 0 ||
           strcmp(operator, "not_equals") == 0 ||
           strcmp(operator, "greater_than") == 0 ||
           strcmp(operator, "less_than") == 0 ||
           strcmp(operator, "greater_or_equal") == 0 ||
           strcmp(operator, "less_or_equal") == 0;
}

static bool rule_from_json(cJSON *json, automation_rule_t *rule, uint32_t default_id)
{
    cJSON *trigger_device = cJSON_GetObjectItem(json, "trigger_device");
    cJSON *trigger_event = cJSON_GetObjectItem(json, "trigger_event");
    cJSON *trigger_value = cJSON_GetObjectItem(json, "trigger_value");
    if (!cJSON_IsString(trigger_device) || !cJSON_IsString(trigger_event) ||
        (!cJSON_IsBool(trigger_value) && !cJSON_IsNumber(trigger_value)) ||
        strlen(trigger_device->valuestring) >= sizeof(rule->trigger_device) ||
        strlen(trigger_event->valuestring) >= sizeof(rule->trigger_event)) {
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
    if (cJSON_IsNumber(trigger_value)) {
        rule->trigger_value_type = EVENT_VALUE_NUMBER;
        rule->trigger_value_number = trigger_value->valuedouble;
    } else {
        rule->trigger_value_type = EVENT_VALUE_BOOL;
        rule->trigger_value_bool = cJSON_IsTrue(trigger_value);
    }
    cJSON *operator = cJSON_GetObjectItem(json, "operator");
    if (cJSON_IsString(operator) && strlen(operator->valuestring) < sizeof(rule->operator)) {
        snprintf(rule->operator, sizeof(rule->operator), "%s", operator->valuestring);
    } else {
        snprintf(rule->operator, sizeof(rule->operator), "equals");
    }
    if (!operator_is_valid(rule->operator) ||
        (rule->trigger_value_type == EVENT_VALUE_BOOL &&
         strcmp(rule->operator, "equals") != 0 &&
         strcmp(rule->operator, "not_equals") != 0)) {
        return false;
    }
    cJSON *actions = cJSON_GetObjectItem(json, "actions");
    if (cJSON_IsArray(actions)) {
        size_t count = (size_t)cJSON_GetArraySize(actions);
        if (count == 0 || count > MAX_RULE_ACTIONS) {
            return false;
        }
        for (size_t i = 0; i < count; i++) {
            cJSON *item = cJSON_GetArrayItem(actions, (int)i);
            cJSON *device = item != NULL ? cJSON_GetObjectItem(item, "device") : NULL;
            cJSON *action = item != NULL ? cJSON_GetObjectItem(item, "action") : NULL;
            cJSON *delay = item != NULL ? cJSON_GetObjectItem(item, "delay_seconds") : NULL;
            cJSON *duration = item != NULL ? cJSON_GetObjectItem(item, "duration_seconds") : NULL;
            if (!cJSON_IsString(device) || !cJSON_IsString(action) ||
                strlen(device->valuestring) >= sizeof(rule->actions[i].device) ||
                strlen(action->valuestring) >= sizeof(rule->actions[i].action) ||
                (strcmp(action->valuestring, "on") != 0 &&
                 strcmp(action->valuestring, "off") != 0) ||
                (delay != NULL && (!cJSON_IsNumber(delay) || delay->valueint < 0)) ||
                (duration != NULL && (!cJSON_IsNumber(duration) || duration->valueint < 0))) {
                return false;
            }
            snprintf(rule->actions[i].device, sizeof(rule->actions[i].device), "%s", device->valuestring);
            snprintf(rule->actions[i].action, sizeof(rule->actions[i].action), "%s", action->valuestring);
            rule->actions[i].delay_seconds = delay != NULL ? (uint32_t)delay->valueint : 0;
            rule->actions[i].duration_seconds = duration != NULL ? (uint32_t)duration->valueint : 0;
        }
        rule->action_count = count;
        return true;
    }
    cJSON *legacy_device = cJSON_GetObjectItem(json, "action_device");
    cJSON *legacy_action = cJSON_GetObjectItem(json, "action");
    if (!cJSON_IsString(legacy_device) || !cJSON_IsString(legacy_action) ||
        strlen(legacy_device->valuestring) >= sizeof(rule->actions[0].device) ||
        strlen(legacy_action->valuestring) >= sizeof(rule->actions[0].action) ||
        (strcmp(legacy_action->valuestring, "on") != 0 &&
         strcmp(legacy_action->valuestring, "off") != 0)) {
        return false;
    }
    snprintf(rule->actions[0].device, sizeof(rule->actions[0].device), "%s", legacy_device->valuestring);
    snprintf(rule->actions[0].action, sizeof(rule->actions[0].action), "%s", legacy_action->valuestring);
    rule->action_count = 1;
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
        if (serialized == NULL || strlen(serialized) >= 1024 ||
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
        if (nvs_get_str(handle, key, NULL, &size) == ESP_OK && size <= 1024) {
            char serialized[1024];
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

static void pending_command_add(const automation_action_t *action, int request_id)
{
    if (xSemaphoreTake(s_command_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG_HTTP, "Nao foi possivel registrar comando pendente");
        return;
    }
    for (size_t i = 0; i < MAX_PENDING_COMMANDS; i++) {
        if (!s_pending_commands[i].active) {
            s_pending_commands[i].active = true;
            s_pending_commands[i].request_id = request_id;
            s_pending_commands[i].sent_at_us = esp_timer_get_time();
            snprintf(s_pending_commands[i].device, sizeof(s_pending_commands[i].device),
                     "%.31s", action->device);
            snprintf(s_pending_commands[i].action, sizeof(s_pending_commands[i].action),
                     "%.23s", action->action);
            s_pending_commands[i].waiter = action->waiter;
            xSemaphoreGive(s_command_mutex);
            return;
        }
    }
    xSemaphoreGive(s_command_mutex);
    ESP_LOGW(TAG_HTTP, "Limite de comandos pendentes atingido");
}

static bool pending_command_finish(int request_id, command_result_t result,
                                   const char *status)
{
    if (xSemaphoreTake(s_command_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }
    for (size_t i = 0; i < MAX_PENDING_COMMANDS; i++) {
        if (s_pending_commands[i].active &&
            s_pending_commands[i].request_id == request_id) {
            s_pending_commands[i].active = false;
            command_waiter_t *waiter = s_pending_commands[i].waiter;
            s_pending_commands[i].waiter = NULL;
            ESP_LOGI(TAG_HTTP, "Comando %d concluido com status=%s",
                     request_id, status);
            if (waiter != NULL) {
                waiter->result = result;
                xSemaphoreGive(waiter->semaphore);
            }
            s_completed_request_ids[s_completed_request_next] = request_id;
            s_completed_request_next =
                (s_completed_request_next + 1) % MAX_PENDING_COMMANDS;
            xSemaphoreGive(s_command_mutex);
            return true;
        }
    }
    xSemaphoreGive(s_command_mutex);
    return false;
}

static bool was_completed_request(int request_id)
{
    for (size_t i = 0; i < MAX_PENDING_COMMANDS; ++i) {
        if (s_completed_request_ids[i] == request_id) {
            return true;
        }
    }
    return false;
}

static void pending_command_cancel(int request_id)
{
    if (xSemaphoreTake(s_command_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    for (size_t i = 0; i < MAX_PENDING_COMMANDS; i++) {
        if (s_pending_commands[i].active &&
            s_pending_commands[i].request_id == request_id) {
            s_pending_commands[i].active = false;
            s_pending_commands[i].waiter = NULL;
            break;
        }
    }
    xSemaphoreGive(s_command_mutex);
}

static void send_command_now(const automation_action_t *queued_action)
{
    char message[192];
    int request_id = queued_action->request_id > 0 ?
                     queued_action->request_id : s_next_request_id++;
    int length = snprintf(message, sizeof(message),
                           "{\"type\":\"command\",\"protocol_version\":%d,\"request_id\":%d,\"device\":\"%s\",\"action\":\"%s\"}",
                           UART_PROTOCOL_VERSION,
                           request_id, queued_action->device, queued_action->action);
    if (length < 0 || (size_t)length >= sizeof(message)) {
        ESP_LOGE(TAG_HTTP, "Acao de automacao muito grande");
        return;
    }
    pending_command_add(queued_action, request_id);
    uart_write_bytes(UART_PORT_NUM, message, length);
    uart_write_bytes(UART_PORT_NUM, "\n", 1);
    s_automation_command_count++;
    history_add("command", queued_action->device, queued_action->action, NULL);
    ESP_LOGI(TAG_HTTP, "Comando executado: %s -> %s",
             queued_action->device, queued_action->action);
}

static bool enqueue_action(const automation_action_t *action)
{
    if (s_action_queue == NULL || xQueueSend(s_action_queue, action, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG_HTTP, "Fila de acoes cheia ou indisponivel");
        return false;
    }
    return true;
}

static void action_timer_callback(TimerHandle_t timer);

static bool start_action_timer(const automation_action_t *action, uint32_t delay_seconds)
{
    automation_action_t *timer_action = malloc(sizeof(*timer_action));
    if (timer_action == NULL) {
        ESP_LOGE(TAG_HTTP, "Sem memoria para temporizador de acao");
        return false;
    }
    *timer_action = *action;
    TimerHandle_t timer = xTimerCreate("auto_action", pdMS_TO_TICKS(delay_seconds * 1000U),
                                       pdFALSE, timer_action, action_timer_callback);
    if (timer == NULL || xTimerStart(timer, pdMS_TO_TICKS(100)) != pdPASS) {
        if (timer != NULL) {
            xTimerDelete(timer, pdMS_TO_TICKS(100));
        }
        free(timer_action);
        ESP_LOGE(TAG_HTTP, "Nao foi possivel iniciar temporizador de acao");
        return false;
    }
    return true;
}

static void action_timer_callback(TimerHandle_t timer)
{
    automation_action_t *action = (automation_action_t *)pvTimerGetTimerID(timer);
    if (action != NULL) {
        automation_action_t ready_action = *action;
        ready_action.delay_seconds = 0;
        if (s_action_queue == NULL ||
            xQueueSend(s_action_queue, &ready_action, 0) != pdTRUE) {
            ESP_LOGE(TAG_HTTP, "Fila cheia; acao temporizada descartada");
        }
        free(action);
    }
    xTimerDelete(timer, 0);
}

static void process_queued_action(const automation_action_t *action)
{
    if (action->delay_seconds > 0) {
        start_action_timer(action, action->delay_seconds);
        return;
    }
    send_command_now(action);
    if (action->duration_seconds > 0) {
        automation_action_t reverse = *action;
        reverse.delay_seconds = 0;
        reverse.duration_seconds = 0;
        snprintf(reverse.action, sizeof(reverse.action), "%s",
                 strcmp(action->action, "on") == 0 ? "off" : "on");
        start_action_timer(&reverse, action->duration_seconds);
    }
}

static void automation_action_task(void *arg)
{
    automation_action_t action;
    while (true) {
        if (xQueueReceive(s_action_queue, &action, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        process_queued_action(&action);
    }
}

static bool rule_condition_matches(const automation_rule_t *rule,
                                   const char *device, const char *event,
                                   const event_value_t *value)
{
    if (!rule->configured || !rule->enabled ||
        strcmp(rule->trigger_device, device) != 0 ||
        strcmp(rule->trigger_event, event) != 0 ||
        rule->trigger_value_type != value->type) {
        return false;
    }
    if (value->type == EVENT_VALUE_BOOL) {
        if (strcmp(rule->operator, "equals") == 0) {
            return rule->trigger_value_bool == value->bool_value;
        }
        return strcmp(rule->operator, "not_equals") == 0 &&
               rule->trigger_value_bool != value->bool_value;
    }
    double actual = value->numeric_value;
    double expected = rule->trigger_value_number;
    if (strcmp(rule->operator, "equals") == 0) return actual == expected;
    if (strcmp(rule->operator, "not_equals") == 0) return actual != expected;
    if (strcmp(rule->operator, "greater_than") == 0) return actual > expected;
    if (strcmp(rule->operator, "less_than") == 0) return actual < expected;
    if (strcmp(rule->operator, "greater_or_equal") == 0) return actual >= expected;
    if (strcmp(rule->operator, "less_or_equal") == 0) return actual <= expected;
    return false;
}

static bool automation_process_event(const char *device, const char *event,
                                     const event_value_t *value)
{
    s_event_count++;
    s_last_event_us = esp_timer_get_time();
    history_add("event", device, event, value);
    bool matched = false;
    if (s_rule_execution_active) {
        return false;
    }
    s_rule_execution_active = true;
    for (size_t i = 0; i < MAX_AUTOMATION_RULES; i++) {
        automation_rule_t *rule = &s_rules[i];
        bool condition_matches = rule_condition_matches(rule, device, event, value);
        if (condition_matches && !rule->condition_active) {
            rule->condition_active = true;
            s_rule_match_count++;
            for (size_t action_index = 0; action_index < rule->action_count; action_index++) {
                if (enqueue_action(&rule->actions[action_index])) {
                    matched = true;
                }
            }
        } else if (!condition_matches) {
            rule->condition_active = false;
        }
    }
    s_rule_execution_active = false;
    return matched;
}

static bool process_wifi_sensor_event(const char *device, const char *event,
                                      const event_value_t *value, const char *unit)
{
    if (value->type != EVENT_VALUE_NUMBER) {
        return automation_process_event(device, event, value);
    }
    if (
        strlen(device) >= sizeof(s_wifi_sensor.device_id) ||
        strlen(event) >= sizeof(s_wifi_sensor.event_name)) {
        return false;
    }

    int64_t now_us = esp_timer_get_time();
    bool accepted = !s_wifi_sensor.value_known ||
                    (now_us - s_wifi_sensor.last_event_us >=
                         (int64_t)WIFI_SENSOR_MIN_INTERVAL_SECONDS * 1000000 &&
                     fabs(value->numeric_value - s_wifi_sensor.value) >= LUMINOSITY_MIN_DELTA);
    if (xSemaphoreTake(s_registry_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG_HTTP, "Nao foi possivel atualizar sensor Wi-Fi");
        return false;
    }
    s_wifi_sensor.discovered = true;
    s_wifi_sensor.online = true;
    s_wifi_sensor.last_event_us = now_us;
    snprintf(s_wifi_sensor.device_id, sizeof(s_wifi_sensor.device_id), "%s", device);
    snprintf(s_wifi_sensor.event_name, sizeof(s_wifi_sensor.event_name), "%s", event);
    if (unit != NULL && strlen(unit) < sizeof(s_wifi_sensor.unit)) {
        snprintf(s_wifi_sensor.unit, sizeof(s_wifi_sensor.unit), "%s", unit);
    }
    if (accepted) {
        s_wifi_sensor.value_known = true;
        s_wifi_sensor.value = value->numeric_value;
        s_wifi_sensor.last_event_us = now_us;
        wifi_sensor_save();
    }
    xSemaphoreGive(s_registry_mutex);

    if (!accepted) {
        ESP_LOGD(TAG_HTTP, "Evento Wi-Fi ignorado por variacao menor que %.1f", LUMINOSITY_MIN_DELTA);
        return false;
    }
    return automation_process_event(device, event, value);
}

static void registry_load(void)
{
    nvs_handle_t handle;
    if (nvs_open("devices", NVS_READONLY, &handle) != ESP_OK) {
        return;
    }

    bool loaded_any = false;
    for (size_t i = 0; i < MAX_ZIGBEE_DEVICES; ++i) {
        char key[16];
        uint8_t discovered = 0;
        uint8_t state_known = 0;
        uint8_t state = 0;
        snprintf(key, sizeof(key), "d%u_used", (unsigned)i);
        if (nvs_get_u8(handle, key, &discovered) != ESP_OK || !discovered) {
            continue;
        }
        zigbee_device_registry_t *device = &s_zigbee_devices[i];
        snprintf(key, sizeof(key), "d%u_id", (unsigned)i);
        size_t id_size = sizeof(device->id);
        if (nvs_get_str(handle, key, device->id, &id_size) != ESP_OK) {
            memset(device, 0, sizeof(*device));
            continue;
        }
        snprintf(key, sizeof(key), "d%u_short", (unsigned)i);
        nvs_get_u16(handle, key, &device->short_address);
        snprintf(key, sizeof(key), "d%u_ep", (unsigned)i);
        nvs_get_u8(handle, key, &device->endpoint);
        snprintf(key, sizeof(key), "d%u_ieee", (unsigned)i);
        size_t ieee_size = sizeof(device->ieee_address);
        nvs_get_str(handle, key, device->ieee_address, &ieee_size);
        snprintf(key, sizeof(key), "d%u_known", (unsigned)i);
        nvs_get_u8(handle, key, &state_known);
        snprintf(key, sizeof(key), "d%u_state", (unsigned)i);
        nvs_get_u8(handle, key, &state);
        device->discovered = true;
        device->online = false;
        device->state_known = state_known != 0;
        device->state = state != 0;
        loaded_any = true;
    }

    if (!loaded_any) {
        uint8_t discovered = 0;
        if (nvs_get_u8(handle, "luz_discovered", &discovered) == ESP_OK &&
            discovered) {
            zigbee_device_registry_t *device = &s_zigbee_devices[0];
            snprintf(device->id, sizeof(device->id), "luz_sala");
            device->discovered = true;
            device->online = false;
            nvs_get_u16(handle, "luz_short", &device->short_address);
            nvs_get_u8(handle, "luz_endpoint", &device->endpoint);
            size_t ieee_size = sizeof(device->ieee_address);
            nvs_get_str(handle, "luz_ieee", device->ieee_address, &ieee_size);
            uint8_t state_known = 0;
            uint8_t state = 0;
            nvs_get_u8(handle, "luz_state_known", &state_known);
            nvs_get_u8(handle, "luz_state", &state);
            device->state_known = state_known != 0;
            device->state = state != 0;
        }
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

    esp_err_t err = ESP_OK;
    for (size_t i = 0; i < MAX_ZIGBEE_DEVICES && err == ESP_OK; ++i) {
        char key[16];
        const zigbee_device_registry_t *device = &s_zigbee_devices[i];
        snprintf(key, sizeof(key), "d%u_used", (unsigned)i);
        err = nvs_set_u8(handle, key, device->discovered ? 1 : 0);
        if (err == ESP_OK && device->discovered) {
            snprintf(key, sizeof(key), "d%u_id", (unsigned)i);
            err = nvs_set_str(handle, key, device->id);
            snprintf(key, sizeof(key), "d%u_short", (unsigned)i);
            if (err == ESP_OK) err = nvs_set_u16(handle, key, device->short_address);
            snprintf(key, sizeof(key), "d%u_ep", (unsigned)i);
            if (err == ESP_OK) err = nvs_set_u8(handle, key, device->endpoint);
            snprintf(key, sizeof(key), "d%u_ieee", (unsigned)i);
            if (err == ESP_OK) err = nvs_set_str(handle, key, device->ieee_address);
            snprintf(key, sizeof(key), "d%u_known", (unsigned)i);
            if (err == ESP_OK) err = nvs_set_u8(handle, key, device->state_known ? 1 : 0);
            snprintf(key, sizeof(key), "d%u_state", (unsigned)i);
            if (err == ESP_OK) err = nvs_set_u8(handle, key, device->state ? 1 : 0);
        }
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG_UART, "Falha ao salvar registro: %s", esp_err_to_name(err));
    }
}

static void wifi_sensor_load(void)
{
    nvs_handle_t handle;
    if (nvs_open("wifi_devices", NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    uint8_t discovered = 0;
    uint8_t value_known = 0;
    size_t size = sizeof(s_wifi_sensor.device_id);
    if (nvs_get_u8(handle, "discovered", &discovered) == ESP_OK &&
        nvs_get_str(handle, "device_id", s_wifi_sensor.device_id, &size) == ESP_OK) {
        size = sizeof(s_wifi_sensor.event_name);
        nvs_get_str(handle, "event_name", s_wifi_sensor.event_name, &size);
        size = sizeof(s_wifi_sensor.unit);
        nvs_get_str(handle, "unit", s_wifi_sensor.unit, &size);
        nvs_get_u8(handle, "value_known", &value_known);
        uint64_t value_bits = 0;
        nvs_get_u64(handle, "value_bits", &value_bits);
        memcpy(&s_wifi_sensor.value, &value_bits, sizeof(value_bits));
        s_wifi_sensor.discovered = discovered != 0;
        s_wifi_sensor.online = false;
        s_wifi_sensor.value_known = value_known != 0;
    }
    nvs_close(handle);
}

static void wifi_sensor_save(void)
{
    nvs_handle_t handle;
    if (nvs_open("wifi_devices", NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGE(TAG_HTTP, "Nao foi possivel abrir NVS do sensor Wi-Fi");
        return;
    }
    esp_err_t err = nvs_set_u8(handle, "discovered", s_wifi_sensor.discovered ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, "device_id", s_wifi_sensor.device_id);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, "event_name", s_wifi_sensor.event_name);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(handle, "unit", s_wifi_sensor.unit);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, "value_known", s_wifi_sensor.value_known ? 1 : 0);
    }
    if (err == ESP_OK) {
        uint64_t value_bits = 0;
        memcpy(&value_bits, &s_wifi_sensor.value, sizeof(value_bits));
        err = nvs_set_u64(handle, "value_bits", value_bits);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_HTTP, "Falha ao salvar sensor Wi-Fi: %s", esp_err_to_name(err));
    }
}

static void wifi_sensor_timeout_task(void *arg)
{
    while (true) {
        if (xSemaphoreTake(s_registry_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (s_wifi_sensor.online &&
                esp_timer_get_time() - s_wifi_sensor.last_event_us >
                    (int64_t)WIFI_SENSOR_TIMEOUT_SECONDS * 1000000) {
                s_wifi_sensor.online = false;
                ESP_LOGW(TAG_HTTP, "Sensor Wi-Fi offline por timeout");
            }
            xSemaphoreGive(s_registry_mutex);
        }
        if (xSemaphoreTake(s_command_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            int64_t now_us = esp_timer_get_time();
            for (size_t i = 0; i < MAX_PENDING_COMMANDS; i++) {
                if (s_pending_commands[i].active &&
                    now_us - s_pending_commands[i].sent_at_us > 10000000) {
                    s_pending_commands[i].active = false;
                    s_command_timeout_count++;
                    command_waiter_t *waiter = s_pending_commands[i].waiter;
                    s_pending_commands[i].waiter = NULL;
                    ESP_LOGW(TAG_HTTP, "Timeout do comando %d",
                             s_pending_commands[i].request_id);
                    if (waiter != NULL) {
                        waiter->result = COMMAND_TIMEOUT;
                        xSemaphoreGive(waiter->semaphore);
                    }
                }
            }
            xSemaphoreGive(s_command_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
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
            .ssid = "",
            .ssid_len = 0,
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .password = "",
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    snprintf((char *)wifi_ap_config.ap.ssid, sizeof(wifi_ap_config.ap.ssid), "%.31s", s_ap_ssid);
    wifi_ap_config.ap.ssid_len = strlen(s_ap_ssid);
    snprintf((char *)wifi_ap_config.ap.password, sizeof(wifi_ap_config.ap.password), "%.63s", s_ap_password);
    if (strlen(s_ap_password) == 0) {
        wifi_ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));
    ESP_LOGI(TAG_AP, "wifi_init_softap finished. SSID:%s channel:%d",
             s_ap_ssid, EXAMPLE_ESP_WIFI_CHANNEL);

    return esp_netif_ap;
}

static esp_netif_t *wifi_init_sta(void)
{
    esp_netif_t *esp_netif_sta = esp_netif_create_default_wifi_sta();

    wifi_config_t wifi_sta_config = {
        .sta = {
            .ssid = "",
            .password = "",
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .failure_retry_cnt = EXAMPLE_ESP_MAXIMUM_RETRY,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };

    snprintf((char *)wifi_sta_config.sta.ssid, sizeof(wifi_sta_config.sta.ssid), "%.31s", s_sta_ssid);
    snprintf((char *)wifi_sta_config.sta.password, sizeof(wifi_sta_config.sta.password), "%s", s_sta_password);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config));
    ESP_LOGI(TAG_STA, "wifi_init_sta finished for SSID:%s.", s_sta_ssid);

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
                        cJSON *protocol_version = cJSON_GetObjectItem(message,
                                                                       "protocol_version");
                        if (!cJSON_IsNumber(protocol_version) ||
                            protocol_version->valueint != UART_PROTOCOL_VERSION) {
                            s_uart_invalid_message_count++;
                            ESP_LOGW(TAG_UART, "Versao de protocolo UART ausente ou incompatível");
                        } else if (!cJSON_IsString(type)) {
                            s_uart_invalid_message_count++;
                            ESP_LOGW(TAG_UART, "Mensagem UART sem tipo");
                        } else if (strcmp(type->valuestring, "response") == 0) {
                            cJSON *status = cJSON_GetObjectItem(message, "status");
                            cJSON *request_id = cJSON_GetObjectItem(message, "request_id");
                            if (cJSON_IsString(status) && cJSON_IsNumber(request_id)) {
                                ESP_LOGI(TAG_UART, "Resposta do C6: status=%s", status->valuestring);
                                if (strcmp(status->valuestring, "success") == 0) {
                                    s_uart_response_count++;
                                } else {
                                    s_uart_error_count++;
                                }
                                if (!pending_command_finish(
                                    request_id->valueint,
                                    strcmp(status->valuestring, "success") == 0
                                        ? COMMAND_SUCCESS : COMMAND_ERROR,
                                    status->valuestring)) {
                                    if (was_completed_request(request_id->valueint)) {
                                        s_uart_duplicate_response_count++;
                                        ESP_LOGW(TAG_UART, "Resposta UART duplicada: %d",
                                                 request_id->valueint);
                                    } else {
                                        s_uart_unknown_response_count++;
                                        ESP_LOGW(TAG_UART, "Resposta UART sem comando pendente: %d",
                                                 request_id->valueint);
                                    }
                                }
                            } else {
                                s_uart_invalid_message_count++;
                                ESP_LOGW(TAG_UART, "Resposta do C6 sem request_id ou status");
                            }
                        } else if (strcmp(type->valuestring, "event") == 0) {
                            cJSON *event = cJSON_GetObjectItem(message, "event");
                            cJSON *event_device = cJSON_GetObjectItem(message, "device");
                            if (cJSON_IsString(event_device) &&
                                cJSON_IsString(event) &&
                                (strcmp(event->valuestring, "device_online") == 0 ||
                                 strcmp(event->valuestring, "device_offline") == 0 ||
                                 strcmp(event->valuestring, "device_removed") == 0)) {
                                zigbee_device_registry_t *device =
                                    find_zigbee_device(event_device->valuestring);
                                if (device != NULL &&
                                    xSemaphoreTake(s_registry_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                                    device->online =
                                        strcmp(event->valuestring, "device_online") == 0;
                                    if (strcmp(event->valuestring, "device_removed") == 0) {
                                        device->discovered = false;
                                        device->state_known = false;
                                    }
                                    registry_save();
                                    xSemaphoreGive(s_registry_mutex);
                                    ESP_LOGI(TAG_UART, "Status Zigbee de %s: %s",
                                      event_device->valuestring, event->valuestring);
                                }
                            } else if (cJSON_IsString(event_device) &&
                                cJSON_IsString(event) &&
                                strcmp(event->valuestring, "device_discovered") == 0) {
                                cJSON *short_address = cJSON_GetObjectItem(message, "short_address");
                                cJSON *endpoint = cJSON_GetObjectItem(message, "endpoint");
                                cJSON *ieee_address = cJSON_GetObjectItem(message, "ieee_address");
                                if (cJSON_IsNumber(short_address) &&
                                    cJSON_IsNumber(endpoint) &&
                                    cJSON_IsString(ieee_address) &&
                                    xSemaphoreTake(s_registry_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                                    zigbee_device_registry_t *device =
                                        get_or_create_zigbee_device(event_device->valuestring);
                                    if (device == NULL) {
                                        xSemaphoreGive(s_registry_mutex);
                                        ESP_LOGW(TAG_UART, "Tabela Zigbee cheia");
                                        cJSON_Delete(message);
                                        continue;
                                    }
                                    device->discovered = true;
                                    device->online = true;
                                    device->short_address = (uint16_t)short_address->valueint;
                                    device->endpoint = (uint8_t)endpoint->valueint;
                                    snprintf(device->ieee_address,
                                             sizeof(device->ieee_address), "%s",
                                             ieee_address->valuestring);
                                    registry_save();
                                    xSemaphoreGive(s_registry_mutex);
                                    ESP_LOGI(TAG_UART, "Dispositivo luz_sala atualizado: short=0x%04x endpoint=%u ieee=%s",
                                             device->short_address, device->endpoint,
                                             device->ieee_address);
                                } else {
                                    ESP_LOGW(TAG_UART, "Evento device_discovered invalido");
                                }
                            } else if (cJSON_IsString(event_device) &&
                                       cJSON_IsString(event) &&
                                       strcmp(event->valuestring, "state_changed") == 0) {
                                cJSON *value = cJSON_GetObjectItem(message, "value");
                                if (cJSON_IsBool(value) &&
                                    xSemaphoreTake(s_registry_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                                    zigbee_device_registry_t *device =
                                        find_zigbee_device(event_device->valuestring);
                                    if (device == NULL) {
                                        ESP_LOGW(TAG_UART, "Estado de dispositivo Zigbee desconhecido: %s",
                                                 event_device->valuestring);
                                        cJSON_Delete(message);
                                        continue;
                                    }
                                    device->state_known = true;
                                    device->state = cJSON_IsTrue(value);
                                    device->online = true;
                                    registry_save();
                                    xSemaphoreGive(s_registry_mutex);
                                    ESP_LOGI(TAG_UART, "Estado luz_sala: %s",
                                             device->state ? "on" : "off");
                                    event_value_t state_value = {
                                        .type = EVENT_VALUE_BOOL,
                                        .bool_value = device->state
                                    };
                                    automation_process_event(device->id, "state_changed", &state_value);
                                } else {
                                    ESP_LOGW(TAG_UART, "Evento state_changed invalido");
                                }
                            } else {
                                ESP_LOGI(TAG_UART, "Evento recebido do C6");
                            }
                        } else {
                            s_uart_invalid_message_count++;
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

    for (size_t i = 0; i < MAX_ZIGBEE_DEVICES; ++i) {
        zigbee_device_registry_t *zigbee = &s_zigbee_devices[i];
        if (zigbee->discovered) {
            cJSON *device = cJSON_CreateObject();
            cJSON_AddStringToObject(device, "id", zigbee->id);
            cJSON_AddStringToObject(device, "type", "on_off");
            cJSON_AddStringToObject(device, "protocol", "zigbee");
            cJSON_AddBoolToObject(device, "online", zigbee->online);
            if (zigbee->state_known) {
                cJSON_AddBoolToObject(device, "state", zigbee->state);
            }
            cJSON_AddNumberToObject(device, "short_address", zigbee->short_address);
            cJSON_AddNumberToObject(device, "endpoint", zigbee->endpoint);
            cJSON_AddStringToObject(device, "ieee_address", zigbee->ieee_address);
            cJSON_AddItemToArray(devices, device);
        }
    }
    if (s_wifi_sensor.discovered) {
        cJSON *sensor = cJSON_CreateObject();
        cJSON_AddStringToObject(sensor, "id", s_wifi_sensor.device_id);
        cJSON_AddStringToObject(sensor, "type", "illuminance");
        cJSON_AddStringToObject(sensor, "protocol", "wifi");
        cJSON_AddBoolToObject(sensor, "online", s_wifi_sensor.online);
        if (s_wifi_sensor.value_known) {
            cJSON_AddNumberToObject(sensor, "value", s_wifi_sensor.value);
        }
        if (s_wifi_sensor.unit[0] != '\0') {
            cJSON_AddStringToObject(sensor, "unit", s_wifi_sensor.unit);
        }
        if (s_wifi_sensor.last_event_us > 0) {
            cJSON_AddNumberToObject(sensor, "last_seen_seconds",
                                    (double)((esp_timer_get_time() -
                                              s_wifi_sensor.last_event_us) / 1000000));
        }
        cJSON_AddItemToArray(devices, sensor);
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
    if (!api_request_is_authenticated(req)) {
        return ESP_FAIL;
    }
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
    if (!api_request_is_authenticated(req)) {
        return ESP_FAIL;
    }
    char body[768];
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
    if (!api_request_is_authenticated(req)) {
        return ESP_FAIL;
    }
    uint32_t id;
    if (!rule_id_from_uri(req, &id)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ID de regra invalido");
        return ESP_FAIL;
    }
    char body[768];
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
    if (!api_request_is_authenticated(req)) {
        return ESP_FAIL;
    }
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
    if (!api_request_is_authenticated(req)) {
        return ESP_FAIL;
    }
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
    if (!cJSON_IsString(device) || !cJSON_IsString(event) ||
        (!cJSON_IsBool(value) && !cJSON_IsNumber(value))) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Campos de evento invalidos");
        return ESP_FAIL;
    }

    event_value_t event_value = {
        .type = cJSON_IsNumber(value) ? EVENT_VALUE_NUMBER : EVENT_VALUE_BOOL,
        .bool_value = cJSON_IsTrue(value),
        .numeric_value = value->valuedouble
    };
    cJSON *unit = root != NULL ? cJSON_GetObjectItem(root, "unit") : NULL;
    bool is_wifi_event = strcmp(req->uri, "/api/events") == 0;
    bool accepted = true;
    bool matched;
    if (is_wifi_event) {
        accepted = !cJSON_IsNumber(value) || !cJSON_IsString(unit) ||
                   strlen(unit->valuestring) < sizeof(s_wifi_sensor.unit);
        matched = accepted
                    ? process_wifi_sensor_event(device->valuestring, event->valuestring,
                                                &event_value,
                                                cJSON_IsString(unit) ? unit->valuestring : NULL)
                    : false;
    } else {
        matched = automation_process_event(device->valuestring, event->valuestring, &event_value);
    }
    cJSON_Delete(root);

    char response[128];
    const char *source = is_wifi_event ? "wifi" : "test";
    snprintf(response, sizeof(response),
             "{\"status\":\"event_processed\",\"source\":\"%s\",\"accepted\":%s,\"rule_matched\":%s}",
             source, accepted ? "true" : "false", matched ? "true" : "false");
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
    cJSON_AddNumberToObject(status, "uart_invalid_messages",
                            s_uart_invalid_message_count);
    cJSON_AddNumberToObject(status, "uart_unknown_responses",
                            s_uart_unknown_response_count);
    cJSON_AddNumberToObject(status, "uart_duplicate_responses",
                            s_uart_duplicate_response_count);
    cJSON_AddNumberToObject(status, "command_timeouts", s_command_timeout_count);
    size_t pending_commands = 0;
    if (s_command_mutex != NULL &&
        xSemaphoreTake(s_command_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (size_t i = 0; i < MAX_PENDING_COMMANDS; i++) {
            if (s_pending_commands[i].active) {
                pending_commands++;
            }
        }
        xSemaphoreGive(s_command_mutex);
    }
    cJSON_AddNumberToObject(status, "pending_commands", pending_commands);
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
    zigbee_device_registry_t *light = find_zigbee_device("luz_sala");
    cJSON_AddBoolToObject(status, "device_discovered",
                          light != NULL && light->discovered);
    cJSON_AddBoolToObject(status, "device_online",
                          light != NULL && light->online);
    cJSON_AddBoolToObject(status, "wifi_sensor_discovered", s_wifi_sensor.discovered);
    cJSON_AddBoolToObject(status, "wifi_sensor_online", s_wifi_sensor.online);

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
    if (!api_request_is_authenticated(req)) {
        return ESP_FAIL;
    }
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
        if (entry->value_is_number) {
            cJSON_AddNumberToObject(item, "value", entry->numeric_value);
        } else if (entry->type[0] == 'e') {
            cJSON_AddBoolToObject(item, "value", entry->bool_value);
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
    if (!api_request_is_authenticated(req)) {
        return ESP_FAIL;
    }
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
    if (device_len >= 32) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ID de dispositivo muito grande");
        return ESP_FAIL;
    }

    int request_id = s_next_request_id++;
    automation_action_t queued_action = {0};
    snprintf(queued_action.device, sizeof(queued_action.device), "%.31s", device_id);
    snprintf(queued_action.action, sizeof(queued_action.action), "%s", action->valuestring);
    queued_action.request_id = request_id;
    command_waiter_t waiter = {
        .semaphore = xSemaphoreCreateBinary(),
        .result = COMMAND_WAITING
    };
    if (waiter.semaphore == NULL) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Sem memoria para aguardar resposta");
        return ESP_FAIL;
    }
    queued_action.waiter = &waiter;
    if (!enqueue_action(&queued_action)) {
        vSemaphoreDelete(waiter.semaphore);
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Fila de acoes indisponivel");
        return ESP_FAIL;
    }

    cJSON_Delete(root);

    if (xSemaphoreTake(waiter.semaphore, pdMS_TO_TICKS(11000)) != pdTRUE) {
        pending_command_cancel(request_id);
        waiter.result = COMMAND_TIMEOUT;
    }
    const char *status = waiter.result == COMMAND_SUCCESS ? "success" :
                         waiter.result == COMMAND_ERROR ? "error" : "timeout";
    char json_response[192];
    snprintf(json_response, sizeof(json_response),
             "{\"status\":\"%s\",\"request_id\":%d,\"device\":\"%s\"}",
             status, request_id, device_id);
    vSemaphoreDelete(waiter.semaphore);
    if (waiter.result == COMMAND_ERROR) {
        httpd_resp_set_status(req, "502 Bad Gateway");
    } else if (waiter.result == COMMAND_TIMEOUT) {
        httpd_resp_set_status(req, "504 Gateway Timeout");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_response);
    return ESP_OK;
}

// HTTP DELETE /api/devices/<id>
static esp_err_t api_device_delete_handler(httpd_req_t *req)
{
    if (!api_request_is_authenticated(req)) {
        return ESP_FAIL;
    }
    const char *prefix = "/api/devices/";
    const char *device_start = strstr(req->uri, prefix);
    if (device_start == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Dispositivo ausente na URL");
        return ESP_FAIL;
    }

    const char *device_id = device_start + strlen(prefix);
    size_t device_len = strlen(device_id);
    if (device_len == 0 || device_len >= 32) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ID de dispositivo invalido");
        return ESP_FAIL;
    }

    int request_id = s_next_request_id++;
    automation_action_t action = {0};
    snprintf(action.device, sizeof(action.device), "%s", device_id);
    snprintf(action.action, sizeof(action.action), "remove_device");
    action.request_id = request_id;
    command_waiter_t waiter = {
        .semaphore = xSemaphoreCreateBinary(),
        .result = COMMAND_WAITING
    };
    if (waiter.semaphore == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Sem memoria para aguardar resposta");
        return ESP_FAIL;
    }
    action.waiter = &waiter;
    if (!enqueue_action(&action)) {
        vSemaphoreDelete(waiter.semaphore);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Fila de acoes indisponivel");
        return ESP_FAIL;
    }

    if (xSemaphoreTake(waiter.semaphore, pdMS_TO_TICKS(11000)) != pdTRUE) {
        pending_command_cancel(request_id);
        waiter.result = COMMAND_TIMEOUT;
    }
    const char *status = waiter.result == COMMAND_SUCCESS ? "success" :
                         waiter.result == COMMAND_ERROR ? "error" : "timeout";
    char response[160];
    snprintf(response, sizeof(response),
             "{\"status\":\"%s\",\"request_id\":%d,\"device\":\"%s\"}",
             status, request_id, device_id);
    vSemaphoreDelete(waiter.semaphore);
    if (waiter.result == COMMAND_ERROR) {
        httpd_resp_set_status(req, "502 Bad Gateway");
    } else if (waiter.result == COMMAND_TIMEOUT) {
        httpd_resp_set_status(req, "504 Gateway Timeout");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    return ESP_OK;
}

// HTTP POST /api/config/auth
static esp_err_t api_auth_config_post_handler(httpd_req_t *req)
{
    if (!api_request_is_authenticated(req)) {
        return ESP_FAIL;
    }

    char buf[128];
    if (req->content_len == 0 || req->content_len >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body invalido");
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

    cJSON *root = cJSON_Parse(buf);
    cJSON *new_token = root != NULL ? cJSON_GetObjectItem(root, "token") : NULL;
    if (!cJSON_IsObject(root) || !cJSON_IsString(new_token) ||
        strlen(new_token->valuestring) < 16 ||
        strlen(new_token->valuestring) > HUB_API_TOKEN_MAX_LENGTH) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "token deve ter entre 16 e 63 caracteres");
        return ESP_FAIL;
    }
    for (const char *cursor = new_token->valuestring; *cursor != '\0'; ++cursor) {
        if ((unsigned char)*cursor < 0x21 || (unsigned char)*cursor > 0x7e) {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                "token deve conter apenas caracteres imprimiveis sem espacos");
            return ESP_FAIL;
        }
    }

    esp_err_t err = api_token_save(new_token->valuestring);
    if (err == ESP_OK) {
        snprintf(s_api_token, sizeof(s_api_token), "%s", new_token->valuestring);
    }
    cJSON_Delete(root);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_HTTP, "Falha ao salvar token: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Falha ao persistir token");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"token_updated\"}");
    return ESP_OK;
}

// HTTP POST /api/config/wifi
static esp_err_t api_wifi_config_post_handler(httpd_req_t *req)
{
    if (!api_request_is_authenticated(req)) {
        return ESP_FAIL;
    }

    char buf[384];
    if (req->content_len == 0 || req->content_len >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body invalido");
        return ESP_FAIL;
    }
    size_t received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, buf + received, req->content_len - received);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (ret <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Falha ao receber body");
            return ESP_FAIL;
        }
        received += (size_t)ret;
    }
    buf[received] = '\0';

    cJSON *root = cJSON_Parse(buf);
    cJSON *sta_ssid = root != NULL ? cJSON_GetObjectItem(root, "sta_ssid") : NULL;
    cJSON *sta_password = root != NULL ? cJSON_GetObjectItem(root, "sta_password") : NULL;
    cJSON *ap_ssid = root != NULL ? cJSON_GetObjectItem(root, "ap_ssid") : NULL;
    cJSON *ap_password = root != NULL ? cJSON_GetObjectItem(root, "ap_password") : NULL;
    if (!cJSON_IsObject(root) || !cJSON_IsString(sta_ssid) ||
        !cJSON_IsString(sta_password) || !cJSON_IsString(ap_ssid) ||
        !cJSON_IsString(ap_password) ||
        strlen(sta_ssid->valuestring) == 0 ||
        strlen(sta_ssid->valuestring) > 32 ||
        strlen(ap_ssid->valuestring) == 0 ||
        strlen(ap_ssid->valuestring) > 32 ||
        strlen(sta_password->valuestring) > WIFI_CONFIG_MAX_LENGTH ||
        (strlen(ap_password->valuestring) != 0 &&
         (strlen(ap_password->valuestring) < 8 ||
          strlen(ap_password->valuestring) > WIFI_CONFIG_MAX_LENGTH))) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "SSID ou senha invalida");
        return ESP_FAIL;
    }

    esp_err_t err = wifi_config_save(sta_ssid->valuestring, sta_password->valuestring,
                                     ap_ssid->valuestring, ap_password->valuestring);
    if (err == ESP_OK) {
        snprintf(s_sta_ssid, sizeof(s_sta_ssid), "%s", sta_ssid->valuestring);
        snprintf(s_sta_password, sizeof(s_sta_password), "%s", sta_password->valuestring);
        snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s", ap_ssid->valuestring);
        snprintf(s_ap_password, sizeof(s_ap_password), "%s", ap_password->valuestring);
    }
    cJSON_Delete(root);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_HTTP, "Falha ao salvar configuracao Wi-Fi: %s",
                 esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Falha ao persistir configuracao");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req,
                       "{\"status\":\"wifi_config_saved\",\"restart_required\":true}");
    return ESP_OK;
}

// HTTP GET /api/config
static esp_err_t api_config_get_handler(httpd_req_t *req)
{
    if (!api_request_is_authenticated(req)) {
        return ESP_FAIL;
    }

    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&mode);
    wifi_ap_record_t ap_info = {0};
    bool sta_connected = esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK;
    uint16_t ap_clients = 0;
    wifi_sta_list_t sta_list = {0};
    if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK) {
        ap_clients = sta_list.num;
    }

    cJSON *config = cJSON_CreateObject();
    if (config == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Falha ao criar resposta");
        return ESP_FAIL;
    }
    cJSON_AddStringToObject(config, "sta_ssid", s_sta_ssid);
    cJSON_AddBoolToObject(config, "sta_connected", sta_connected);
    cJSON_AddStringToObject(config, "ap_ssid", s_ap_ssid);
    cJSON_AddNumberToObject(config, "ap_connected_clients", ap_clients);
    cJSON_AddBoolToObject(config, "ap_enabled",
                          mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA);
    cJSON_AddBoolToObject(config, "restart_required", false);

    char *response = cJSON_PrintUnformatted(config);
    cJSON_Delete(config);
    if (response == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Falha ao serializar resposta");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
    free(response);
    return ESP_OK;
}

// HTTP POST /api/zigbee/permit-join
static esp_err_t api_permit_join_post_handler(httpd_req_t *req)
{
    if (!api_request_is_authenticated(req)) {
        return ESP_FAIL;
    }
    char buf[128];
    if (req->content_len == 0 || req->content_len >= sizeof(buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body invalido");
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

    cJSON *root = cJSON_Parse(buf);
    cJSON *duration = root != NULL ? cJSON_GetObjectItem(root, "duration_seconds") : NULL;
    if (!cJSON_IsObject(root) || !cJSON_IsNumber(duration) ||
        duration->valueint < 1 || duration->valueint > 180) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "duration_seconds deve estar entre 1 e 180");
        return ESP_FAIL;
    }
    int duration_seconds = duration->valueint;

    int request_id = s_next_request_id++;
    automation_action_t action = {0};
    snprintf(action.device, sizeof(action.device), "zigbee");
    snprintf(action.action, sizeof(action.action), "permit");
    action.request_id = request_id;
    command_waiter_t waiter = {
        .semaphore = xSemaphoreCreateBinary(),
        .result = COMMAND_WAITING
    };
    if (waiter.semaphore == NULL) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Sem memoria para aguardar resposta");
        return ESP_FAIL;
    }
    action.waiter = &waiter;
    pending_command_add(&action, request_id);

    char message[192];
    int length = snprintf(message, sizeof(message),
                           "{\"type\":\"command\",\"protocol_version\":%d,\"request_id\":%d,"
                           "\"device\":\"zigbee\",\"action\":\"permit_join\","
                           "\"duration_seconds\":%d}",
                           UART_PROTOCOL_VERSION, request_id, duration_seconds);
    cJSON_Delete(root);
    if (length < 0 || (size_t)length >= sizeof(message)) {
        pending_command_cancel(request_id);
        vSemaphoreDelete(waiter.semaphore);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Comando muito grande");
        return ESP_FAIL;
    }

    uart_write_bytes(UART_PORT_NUM, message, length);
    uart_write_bytes(UART_PORT_NUM, "\n", 1);
    if (xSemaphoreTake(waiter.semaphore, pdMS_TO_TICKS(11000)) != pdTRUE) {
        pending_command_cancel(request_id);
        waiter.result = COMMAND_TIMEOUT;
    }

    const char *status = waiter.result == COMMAND_SUCCESS ? "success" :
                         waiter.result == COMMAND_ERROR ? "error" : "timeout";
    char response[160];
    snprintf(response, sizeof(response),
             "{\"status\":\"%s\",\"request_id\":%d,\"duration_seconds\":%d}",
             status, request_id, duration_seconds);
    vSemaphoreDelete(waiter.semaphore);
    if (waiter.result == COMMAND_ERROR) {
        httpd_resp_set_status(req, "502 Bad Gateway");
    } else if (waiter.result == COMMAND_TIMEOUT) {
        httpd_resp_set_status(req, "504 Gateway Timeout");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);
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
    s_command_mutex = xSemaphoreCreateMutex();
    s_action_queue = xQueueCreate(ACTION_QUEUE_LENGTH, sizeof(automation_action_t));
    ESP_ERROR_CHECK(s_registry_mutex != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(s_history_mutex != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(s_command_mutex != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(s_action_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    registry_load();
    wifi_sensor_load();
    rules_load();
    api_token_load();
    wifi_config_load();

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
        ESP_LOGI(TAG_STA, "connected to configured STA SSID:%s", s_sta_ssid);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG_STA, "Failed to connect to configured SSID:%s", s_sta_ssid);
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
    xTaskCreate(wifi_sensor_timeout_task, "wifi_sensor_timeout", 3072, NULL, 4, NULL);
    xTaskCreate(automation_action_task, "automation_actions", 6144, NULL, 5, NULL);

    // Start HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.max_uri_handlers = 15;
    config.uri_match_fn = httpd_uri_match_wildcard;

    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &config));

    httpd_uri_t uri_root_get = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = api_root_get_handler,
        .user_ctx = NULL
    };

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

    httpd_uri_t uri_permit_join_post = {
        .uri = "/api/zigbee/permit-join",
        .method = HTTP_POST,
        .handler = api_permit_join_post_handler,
        .user_ctx = NULL
    };

    httpd_uri_t uri_device_delete = {
        .uri = "/api/devices/*",
        .method = HTTP_DELETE,
        .handler = api_device_delete_handler,
        .user_ctx = NULL
    };

    httpd_uri_t uri_auth_config_post = {
        .uri = "/api/config/auth",
        .method = HTTP_POST,
        .handler = api_auth_config_post_handler,
        .user_ctx = NULL
    };

    httpd_uri_t uri_wifi_config_post = {
        .uri = "/api/config/wifi",
        .method = HTTP_POST,
        .handler = api_wifi_config_post_handler,
        .user_ctx = NULL
    };

    httpd_uri_t uri_config_get = {
        .uri = "/api/config",
        .method = HTTP_GET,
        .handler = api_config_get_handler,
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

    httpd_uri_t uri_wifi_event_post = {
        .uri = "/api/events",
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

    httpd_register_uri_handler(server, &uri_root_get);
    httpd_register_uri_handler(server, &uri_devices_get);
    httpd_register_uri_handler(server, &uri_device_command_post);
    httpd_register_uri_handler(server, &uri_permit_join_post);
    httpd_register_uri_handler(server, &uri_device_delete);
    httpd_register_uri_handler(server, &uri_auth_config_post);
    httpd_register_uri_handler(server, &uri_wifi_config_post);
    httpd_register_uri_handler(server, &uri_config_get);
    httpd_register_uri_handler(server, &uri_rules_get);
    httpd_register_uri_handler(server, &uri_rules_post);
    httpd_register_uri_handler(server, &uri_rule_put);
    httpd_register_uri_handler(server, &uri_rule_delete);
    httpd_register_uri_handler(server, &uri_test_event_post);
    httpd_register_uri_handler(server, &uri_wifi_event_post);
    httpd_register_uri_handler(server, &uri_status_get);
    httpd_register_uri_handler(server, &uri_history_get);

    ESP_LOGI(TAG_HTTP, "HTTP server started on port %d", config.server_port);
}
