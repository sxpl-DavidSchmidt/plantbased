#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#define AP_SSID "plantbased_ap"
#define AP_PASSWD "password"
#define AP_CHANNEL 1

const char *TAG = "plantbased";

typedef struct
{
    char ssid[32];
    char password[64];
} network_config;

typedef struct
{
    uint8_t id;
    network_config network;
} device_config;

static device_config cfg = {
    .id = 0,
    .network = {
        .ssid = "",
        .password = ""}};

static void read_config(void)
{
    // TODO
}

static void save_config(void)
{
    // TODO
}

/* Endpoint
/cfg    get current configuration
/cfg/set?<var>=<val>    set variables in config
*/

static esp_err_t get_config_handler(httpd_req_t *req)
{
    esp_err_t err = ESP_OK;

    // Build JSON object
    cJSON *root = cJSON_CreateObject();

    cJSON *id = cJSON_CreateNumber(cfg.id);
    cJSON_AddItemToObject(root, "id", id);

    cJSON *network = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "network", network);

    cJSON_AddStringToObject(network, "ssid", cfg.network.ssid);
    cJSON_AddStringToObject(network, "password", cfg.network.password);

    // Serialize JSON object
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    err = httpd_resp_send(req, json_str, HTTPD_RESP_USE_STRLEN);

    free(json_str);
    return err;
}

static const httpd_uri_t get_config_uri = {
    .uri = "/conf",
    .method = HTTP_GET,
    .handler = get_config_handler,
    .user_ctx = NULL};

static esp_err_t set_config_handler(httpd_req_t *req)
{
    esp_err_t err;
    char query[256];
    char id_str[32];
    char ssid[32];
    char password[64];

    err = httpd_req_get_url_query_str(req, query, sizeof(query));
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "No query string found in \"%s\"", req->uri);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing query string");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Received query: %s", query);

    // Handle "id" query
    err = httpd_query_key_value(query, "id", id_str, sizeof(id_str));
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Updating id: %s", id_str);
        cfg.id = atoi(id_str);
    }

    // Handle "ssid" query
    err = httpd_query_key_value(query, "ssid", ssid, sizeof(ssid));
    if (err == ESP_OK)
    {
        if (strlen(ssid) >= sizeof(cfg.network.ssid))
        {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID too long");
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "Updating network.ssid: %s", ssid);
        strlcpy(cfg.network.ssid, ssid, sizeof(cfg.network.ssid));
    }

    // Handle "password" query
    err = httpd_query_key_value(query, "password", password, sizeof(password));
    if (err == ESP_OK)
    {
        if (strlen(password) >= sizeof(cfg.network.password))
        {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Password too long");
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "Updating network.password: %s", password);
        strlcpy(cfg.network.password, password, sizeof(cfg.network.password));
    }

    // Save changes
    save_config();

    // Send response
    char resp[32];
    snprintf(resp, sizeof(resp), "Config updated\n");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static const httpd_uri_t set_config_uri = {
    .uri = "/conf/set",
    .method = HTTP_GET,
    .handler = set_config_handler,
    .user_ctx = NULL};

void enter_config_mode()
{
    esp_err_t err;

    err = esp_netif_init();
    err = esp_event_loop_create_default();

    // Initialize NVS
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Initialize WiFi
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wifi_cfg);
    if (err == ESP_ERR_NO_MEM)
    {
        ESP_LOGE(TAG, "Failed to initiate WiFi: Out of memory.");
    }

    // Initialize AP
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_netif_t *ap = esp_netif_create_default_wifi_ap();
    wifi_config_t ap_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .channel = AP_CHANNEL,
            .password = AP_PASSWD,
            .max_connection = 1,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = false}}};

    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    ESP_LOGI(TAG, "Initialized AP. SSID: %s, password: %s, channel: %d", AP_SSID, AP_PASSWD, AP_CHANNEL);

    err = esp_wifi_start();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(err));
    }

    httpd_handle_t server = NULL;
    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    httpd_start(&server, &server_config);
    httpd_register_uri_handler(server, &set_config_uri);
    httpd_register_uri_handler(server, &get_config_uri);
}

void app_main(void)
{
    enter_config_mode();
}