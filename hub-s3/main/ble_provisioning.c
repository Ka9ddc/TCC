#include "ble_provisioning.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "BLE_PROVISION";

static const ble_uuid128_t service_uuid =
    BLE_UUID128_INIT(0x7d, 0x4c, 0x1a, 0x9e, 0x2f, 0x40, 0x4e, 0x11,
                     0x9a, 0x01, 0x52, 0x55, 0x4d, 0x45, 0x48, 0x01);

static const ble_uuid128_t status_uuid =
    BLE_UUID128_INIT(0x7d, 0x4c, 0x1a, 0x9e, 0x2f, 0x40, 0x4e, 0x11,
                     0x9a, 0x01, 0x52, 0x55, 0x4d, 0x45, 0x48, 0x02);

static const ble_uuid128_t wifi_uuid =
    BLE_UUID128_INIT(0x7d, 0x4c, 0x1a, 0x9e, 0x2f, 0x40, 0x4e, 0x11,
                     0x9a, 0x01, 0x52, 0x55, 0x4d, 0x45, 0x48, 0x03);

static const ble_uuid128_t scan_uuid =
    BLE_UUID128_INIT(0x7d, 0x4c, 0x1a, 0x9e, 0x2f, 0x40, 0x4e, 0x11,
                     0x9a, 0x01, 0x52, 0x55, 0x4d, 0x45, 0x48, 0x04);

static const ble_uuid128_t command_uuid =
    BLE_UUID128_INIT(0x7d, 0x4c, 0x1a, 0x9e, 0x2f, 0x40, 0x4e, 0x11,
                     0x9a, 0x01, 0x52, 0x55, 0x4d, 0x45, 0x48, 0x05);

static uint16_t s_connection_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_status_handle;
static uint16_t s_scan_handle;
static bool s_running;
static uint8_t s_addr_type;

static char s_state[32] = "BLE_ONLY";
static char s_wifi_ip[16] = "";
static ble_wifi_save_callback_t s_save_callback;

static int gap_event(struct ble_gap_event *event, void *arg);

static int send_text(struct os_mbuf *om, const char *text)
{
    if (os_mbuf_append(om, text, strlen(text)) != 0) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    return 0;
}

static void notify_status(void)
{
    if (s_connection_handle == BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGW(TAG, "Status BLE nao enviado: nenhum cliente conectado");
        return;
    }

    bool wifi_connected =
        strcmp(s_state, "WIFI_CONNECTED") == 0;

    char payload[160];

    snprintf(
        payload,
        sizeof(payload),
        "{\"type\":\"hub_status\","
        "\"state\":\"%s\","
        "\"wifi_connected\":%s,"
        "\"ip\":\"%s\","
        "\"version\":1}",
        s_state,
        wifi_connected ? "true" : "false",
        s_wifi_ip
    );

    struct os_mbuf *om =
        ble_hs_mbuf_from_flat(payload, strlen(payload));

    if (om == NULL) {
        ESP_LOGW(TAG, "Falha ao criar pacote de status BLE");
        return;
    }

    int rc = ble_gatts_notify_custom(
        s_connection_handle,
        s_status_handle,
        om
    );

    if (rc != 0) {
        ESP_LOGW(TAG, "Falha ao enviar notificacao BLE: %d", rc);
    } else {
        ESP_LOGI(TAG, "Status BLE enviado: %s", s_state);
    }
}

static int handle_wifi_write(struct os_mbuf *om)
{
    char payload[256];
    int length = OS_MBUF_PKTLEN(om);

    if (length <= 0 || length >= (int)sizeof(payload) ||
        ble_hs_mbuf_to_flat(
            om,
            payload,
            sizeof(payload) - 1,
            NULL
        ) != 0) {
        ESP_LOGW(TAG, "Payload Wi-Fi BLE invalido");
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    payload[length] = '\0';

    ESP_LOGI(
        TAG,
        "Payload Wi-Fi BLE recebido (%d bytes): %s",
        length,
        payload
    );

    cJSON *root = cJSON_Parse(payload);

    if (root == NULL) {
        ESP_LOGW(TAG, "JSON Wi-Fi BLE invalido");
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    cJSON *type = cJSON_GetObjectItem(root, "type");
    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *password = cJSON_GetObjectItem(root, "password");

    bool valid =
        cJSON_IsString(type) &&
        strcmp(type->valuestring, "wifi_config") == 0 &&
        cJSON_IsString(ssid) &&
        cJSON_IsString(password) &&
        strlen(ssid->valuestring) > 0 &&
        strlen(ssid->valuestring) <= 32 &&
        strlen(password->valuestring) <= 63;

    if (!valid) {
        ESP_LOGW(TAG, "Campos Wi-Fi BLE invalidos");
        cJSON_Delete(root);
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (s_save_callback == NULL) {
        ESP_LOGE(TAG, "Callback de salvamento Wi-Fi nao configurado");
        cJSON_Delete(root);
        return BLE_ATT_ERR_UNLIKELY;
    }

    esp_err_t err = s_save_callback(
        ssid->valuestring,
        password->valuestring
    );

    cJSON_Delete(root);

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Falha ao salvar/aplicar Wi-Fi: %s",
            esp_err_to_name(err)
        );

        snprintf(s_state, sizeof(s_state), "%s", "WIFI_FAILED");
        notify_status();

        return BLE_ATT_ERR_UNLIKELY;
    }

    snprintf(s_state, sizeof(s_state), "%s", "CREDENTIALS_RECEIVED");
    notify_status();

    snprintf(s_state, sizeof(s_state), "%s", "WIFI_CONNECTING");
    notify_status();

    return 0;
}

static int handle_command_write(struct os_mbuf *om)
{
    char command[96];
    int length = OS_MBUF_PKTLEN(om);

    if (length <= 0 || length >= (int)sizeof(command) ||
        ble_hs_mbuf_to_flat(
            om,
            command,
            sizeof(command) - 1,
            NULL
        ) != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    command[length] = '\0';

    if (strcmp(command, "status") == 0) {
        notify_status();
        return 0;
    }

    if (strcmp(command, "scan") != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    wifi_scan_config_t config = {
        .show_hidden = true
    };

    if (esp_wifi_scan_start(&config, true) != ESP_OK) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint16_t count = 12;
    wifi_ap_record_t records[12];

    if (esp_wifi_scan_get_ap_records(&count, records) != ESP_OK) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    char payload[512] = "{\"networks\":[";

    for (uint16_t i = 0; i < count; ++i) {
        char item[96];

        snprintf(
            item,
            sizeof(item),
            "%s{\"ssid\":\"%.*s\",\"rssi\":%d,\"auth\":%d}",
            i == 0 ? "" : ",",
            sizeof(records[i].ssid),
            records[i].ssid,
            records[i].rssi,
            records[i].authmode
        );

        if (strlen(payload) + strlen(item) + 2 >= sizeof(payload)) {
            break;
        }

        strncat(
            payload,
            item,
            sizeof(payload) - strlen(payload) - 1
        );
    }

    strncat(
        payload,
        "]}",
        sizeof(payload) - strlen(payload) - 1
    );

    struct os_mbuf *response =
        ble_hs_mbuf_from_flat(payload, strlen(payload));

    if (response == NULL) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    return ble_gatts_notify_custom(
        s_connection_handle,
        s_scan_handle,
        response
    );
}

static int gatt_access(
    uint16_t conn_handle,
    uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt,
    void *arg
)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        if (attr_handle == s_status_handle) {
            char payload[160];

            bool wifi_connected =
                strcmp(s_state, "WIFI_CONNECTED") == 0;

            snprintf(
                payload,
                sizeof(payload),
                "{\"type\":\"hub_status\","
                "\"state\":\"%s\","
                "\"wifi_connected\":%s,"
                "\"ip\":\"%s\","
                "\"version\":1}",
                s_state,
                wifi_connected ? "true" : "false",
                s_wifi_ip
            );

            return send_text(ctxt->om, payload);
        }

        return BLE_ATT_ERR_READ_NOT_PERMITTED;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        const ble_uuid128_t *uuid = (const ble_uuid128_t *)arg;

        if (ble_uuid_cmp(&uuid->u, &wifi_uuid.u) == 0) {
            return handle_wifi_write(ctxt->om);
        }

        if (ble_uuid_cmp(&uuid->u, &command_uuid.u) == 0) {
            return handle_command_write(ctxt->om);
        }
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &status_uuid.u,
                .access_cb = gatt_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_status_handle
            },
            {
                .uuid = &wifi_uuid.u,
                .access_cb = gatt_access,
                .arg = (void *)&wifi_uuid,
                .flags = BLE_GATT_CHR_F_WRITE
            },
            {
                .uuid = &scan_uuid.u,
                .access_cb = gatt_access,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_scan_handle
            },
            {
                .uuid = &command_uuid.u,
                .access_cb = gatt_access,
                .arg = (void *)&command_uuid,
                .flags = BLE_GATT_CHR_F_WRITE
            },
            { 0 }
        },
    },
    { 0 }
};

static void advertise(void)
{
    struct ble_hs_adv_fields fields = {0};

    fields.name = (uint8_t *)"MeuHub";
    fields.name_len = 6;
    fields.name_is_complete = 1;

    fields.uuids128 = &service_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);

    if (rc != 0) {
        ESP_LOGE(TAG, "Falha ao configurar advertising BLE: %d", rc);
        return;
    }

    struct ble_gap_adv_params params = {0};

    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(
        s_addr_type,
        NULL,
        BLE_HS_FOREVER,
        &params,
        gap_event,
        NULL
    );

    if (rc != 0) {
        ESP_LOGE(TAG, "Falha ao iniciar advertising BLE: %d", rc);
        return;
    }

    ESP_LOGI(TAG, "BLE anunciado como MeuHub");
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    if (event->type == BLE_GAP_EVENT_CONNECT) {
        s_connection_handle = event->connect.conn_handle;
        ESP_LOGI(TAG, "Cliente BLE conectado");
        return 0;
    }

    if (event->type == BLE_GAP_EVENT_DISCONNECT) {
        s_connection_handle = BLE_HS_CONN_HANDLE_NONE;
        ESP_LOGI(TAG, "Cliente BLE desconectado");
        advertise();
    }

    return 0;
}

static void on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &s_addr_type);

    if (rc != 0) {
        ESP_LOGE(TAG, "Falha ao inferir endereco BLE: %d", rc);
        return;
    }

    ESP_LOGI(TAG, "BLE sincronizado; iniciando modo de provisionamento");
    advertise();
}

static void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_provisioning_start(ble_wifi_save_callback_t save_callback)
{
    if (s_running) {
        return ESP_OK;
    }

    s_save_callback = save_callback;

    esp_err_t err = nimble_port_init();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao inicializar NimBLE: %s", esp_err_to_name(err));
        s_save_callback = NULL;
        return err;
    }

    ble_svc_gap_init();
    ble_svc_gatt_init();

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.gatts_register_cb = NULL;

    ESP_RETURN_ON_ERROR(
        ble_gatts_count_cfg(services),
        TAG,
        "Falha ao contar GATT"
    );

    ESP_RETURN_ON_ERROR(
        ble_gatts_add_svcs(services),
        TAG,
        "Falha ao registrar GATT"
    );

    snprintf(s_state, sizeof(s_state), "%s", "BLE_ONLY");

    s_running = true;

    nimble_port_freertos_init(host_task);

    ESP_LOGI(TAG, "Provisionamento BLE iniciado");

    return ESP_OK;
}

bool ble_provisioning_is_running(void)
{
    return s_running;
}

void ble_provisioning_set_wifi_connected(bool connected, const char *ip)
{
    if (connected) {
        snprintf(
            s_state,
            sizeof(s_state),
            "%s",
            "WIFI_CONNECTED"
        );
    } else {
        snprintf(
            s_state,
            sizeof(s_state),
            "%s",
            "WIFI_CONNECTING"
        );
    }

    snprintf(s_wifi_ip, sizeof(s_wifi_ip), "%s", ip != NULL ? ip : "");
    ESP_LOGI(TAG, "Estado Wi-Fi BLE atualizado: %s", s_state);
    notify_status();
}