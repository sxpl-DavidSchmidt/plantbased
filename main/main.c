#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#define RESET_GPIO GPIO_NUM_25

#define AP_SSID "plantbased_ap"
#define AP_PASSWD "password"
#define AP_CHANNEL 1

const char *TAG = "plantbased";

static QueueHandle_t gpio_button_event_queue = NULL;

/* handle configuration */
typedef struct
{
    char ssid[32];
    char password[64];
} network_config_t;

typedef enum
{
    MODE_CONFIG,
    MODE_OPERATING
} system_mode_t;

const char *system_mode_to_string(system_mode_t mode)
{
    switch (mode)
    {
    case MODE_CONFIG:
        return "config";
    case MODE_OPERATING:
        return "operating";
    default:
        return "unknown";
    }
}

typedef struct
{
    uint8_t id;
    system_mode_t mode;
    network_config_t network;
} device_config_t;

static device_config_t cfg = {
    .id = 0,
    .mode = MODE_CONFIG,
    .network = {
        .ssid = "",
        .password = ""}};

static esp_err_t cfg_init(void)
{
    ESP_LOGI(TAG, "Initiating config");
    esp_err_t err;

    // init handle
    nvs_handle_t config_handle;
    err = nvs_open("config", NVS_READWRITE, &config_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    bool do_commit = false;

    uint8_t dummy_id;
    err = nvs_get_u8(config_handle, "id", &dummy_id);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        nvs_set_u8(config_handle, "id", cfg.id);
        do_commit = true;
    }
    else if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error reading 'id': %s", esp_err_to_name(err));
        nvs_close(config_handle);
        return err;
    }

    uint8_t dummy_mode;
    err = nvs_get_u8(config_handle, "mode", &dummy_mode);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        nvs_set_u8(config_handle, "mode", (uint8_t)cfg.mode);
        do_commit = true;
    }
    else if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error reading 'mode': %s", esp_err_to_name(err));
        nvs_close(config_handle);
        return err;
    }

    size_t dummy_ssid_str_size = 0;
    err = nvs_get_str(config_handle, "ssid", NULL, &dummy_ssid_str_size);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        nvs_set_str(config_handle, "ssid", cfg.network.ssid);
        do_commit = true;
    }
    else if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error reading 'ssid': %s", esp_err_to_name(err));
        nvs_close(config_handle);
        return err;
    }

    size_t dummy_password_str_size = 0;
    err = nvs_get_str(config_handle, "password", NULL, &dummy_password_str_size);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        nvs_set_str(config_handle, "password", cfg.network.ssid);
        do_commit = true;
    }
    else if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error reading 'password': %s", esp_err_to_name(err));
        nvs_close(config_handle);
        return err;
    }

    if (do_commit)
    {
        nvs_commit(config_handle);
    }

    nvs_close(config_handle);
    return ESP_OK;
}

static esp_err_t cfg_read(void)
{
    ESP_LOGI(TAG, "Reading config from NVS");
    esp_err_t err;

    // init handle
    nvs_handle_t config_handle;
    err = nvs_open("config", NVS_READWRITE, &config_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    // read id
    uint8_t read_id;
    err = nvs_get_u8(config_handle, "id", &read_id);
    switch (err)
    {
    case ESP_OK:
        ESP_LOGI(TAG, "Read id: \"%d\"", read_id);
        cfg.id = read_id;
        break;
    case ESP_ERR_NVS_NOT_FOUND:
        ESP_LOGE(TAG, "id not found in NVS storage");
        nvs_close(config_handle);
        return err;
    default:
        ESP_LOGE(TAG, "Failed to read id: %s", esp_err_to_name(err));
        nvs_close(config_handle);
        return err;
    }

    // read mode
    uint8_t read_mode;
    err = nvs_get_u8(config_handle, "mode", &read_mode);
    switch (err)
    {
    case ESP_OK:
        system_mode_t mode = (system_mode_t)read_mode;
        ESP_LOGI(TAG, "Read mode: \"%d\"", mode);
        cfg.mode = mode;
        break;
    case ESP_ERR_NVS_NOT_FOUND:
        ESP_LOGE(TAG, "mode not found in NVS storage");
        nvs_close(config_handle);
        return err;
    default:
        ESP_LOGE(TAG, "Failed to read mode: %s", esp_err_to_name(err));
        nvs_close(config_handle);
        return err;
    }

    // read ssid
    size_t ssid_str_size = 0;
    err = nvs_get_str(config_handle, "ssid", NULL, &ssid_str_size);
    if (err == ESP_OK)
    {
        char *ssid = malloc(ssid_str_size);
        err = nvs_get_str(config_handle, "ssid", ssid, &ssid_str_size);

        switch (err)
        {
        case ESP_OK:
            ESP_LOGI(TAG, "Read ssid: \"%s\".", ssid);
            strlcpy(cfg.network.ssid, ssid, sizeof(cfg.network.ssid));
            break;
        case ESP_ERR_NVS_NOT_FOUND:
            ESP_LOGE(TAG, "ssid not found in NVS storage");
            nvs_close(config_handle);
            return err;
        default:
            ESP_LOGE(TAG, "Failed to read ssid: %s", esp_err_to_name(err));
            nvs_close(config_handle);
            return err;
        }

        free(ssid);
    }

    // read password
    size_t password_str_size = 0;
    err = nvs_get_str(config_handle, "password", NULL, &password_str_size);
    if (err == ESP_OK)
    {
        char *password = malloc(password_str_size);
        err = nvs_get_str(config_handle, "password", password, &password_str_size);

        switch (err)
        {
        case ESP_OK:
            ESP_LOGI(TAG, "Read password: \"%s\".", password);
            strlcpy(cfg.network.password, password, sizeof(cfg.network.password));
            break;
        case ESP_ERR_NVS_NOT_FOUND:
            ESP_LOGE(TAG, "Password not found in NVS storage");
            nvs_close(config_handle);
            return err;
        default:
            ESP_LOGE(TAG, "Failed to read password: %s", esp_err_to_name(err));
            nvs_close(config_handle);
            return err;
        }

        free(password);
    }

    nvs_close(config_handle);
    return ESP_OK;
}

static esp_err_t cfg_write(void)
{
    ESP_LOGI(TAG, "Writing config to NVS");
    esp_err_t err;

    // init handle
    nvs_handle_t config_handle;
    err = nvs_open("config", NVS_READWRITE, &config_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    // write id
    err = nvs_set_u8(config_handle, "id", cfg.id);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Wrote id \"%d\" to NVS storage", cfg.id);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to write id to NVS storage: %s", esp_err_to_name(err));
        nvs_close(config_handle);
        return err;
    }

    // write mode
    uint8_t write_mode = (uint8_t)cfg.mode;
    err = nvs_set_u8(config_handle, "mode", write_mode);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Wrote mode \"%d\" to NVS storage", write_mode);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to write mode to NVS storage: %s", esp_err_to_name(err));
        nvs_close(config_handle);
        return err;
    }

    // write ssid
    err = nvs_set_str(config_handle, "ssid", cfg.network.ssid);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Wrote ssid \"%s\" to NVS storage", cfg.network.ssid);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to write ssid to NVS storage: %s", esp_err_to_name(err));
        nvs_close(config_handle);
        return err;
    }

    // write password
    err = nvs_set_str(config_handle, "password", cfg.network.password);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Wrote password \"%s\" to NVS storage", cfg.network.password);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to write password to NVS storage: %s", esp_err_to_name(err));
        nvs_close(config_handle);
        return err;
    }

    // commit changes
    err = nvs_commit(config_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(err));
        nvs_close(config_handle);
        return err;
    }

    nvs_close(config_handle);
    return ESP_OK;
}

static esp_err_t cfg_reset(void)
{
    ESP_LOGW(TAG, "Erasing entire NVS partition");
    esp_err_t err;

    err = nvs_flash_erase();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS erase failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_flash_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS re-init failed: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

/* handle GPIO */
void gpio_reset_button_handler(void)
{
    ESP_LOGI(TAG, "RESET button pressed!");
    cfg_reset();
    esp_restart();
}

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(gpio_button_event_queue, &gpio_num, &xHigherPriorityTaskWoken);

    if (xHigherPriorityTaskWoken == pdTRUE)
    {
        portYIELD_FROM_ISR();
    }
}

static void gpio_button_task(void *arg)
{
    uint32_t gpio_num;
    for (;;)
    {
        if (xQueueReceive(gpio_button_event_queue, &gpio_num, portMAX_DELAY))
        {
            switch (gpio_num)
            {
            case RESET_GPIO:
                gpio_reset_button_handler();
                break;
            default:
                break;
            }
        }
    }
}

static void gpio_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RESET_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE};
    gpio_config(&io_conf);

    gpio_button_event_queue = xQueueCreate(10, sizeof(uint32_t));
    xTaskCreate(gpio_button_task, "button_task", 2048, NULL, 10, NULL);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(RESET_GPIO, gpio_isr_handler, (void *)RESET_GPIO);

    ESP_LOGI(TAG, "GPIOs initialized (reset=%d)", RESET_GPIO);
}

/* handle http server */
static esp_err_t get_config_handler(httpd_req_t *req)
{
    esp_err_t err = ESP_OK;

    // Build JSON object
    cJSON *root = cJSON_CreateObject();

    cJSON *id = cJSON_CreateNumber(cfg.id);
    cJSON_AddItemToObject(root, "id", id);

    cJSON_AddStringToObject(root, "mode", system_mode_to_string(cfg.mode));

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
    char query[512];
    char id_str[32];
    char mode_str[32];
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

    // handle "id" query
    err = httpd_query_key_value(query, "id", id_str, sizeof(id_str));
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Updating id: %s", id_str);
        cfg.id = atoi(id_str);
    }

    // handle "mode" query
    err = httpd_query_key_value(query, "mode", mode_str, sizeof(mode_str));
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Updating mode: %s", mode_str);
        cfg.mode = (system_mode_t)atoi(mode_str);
    }

    // handle "ssid" query
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

    // handle "password" query
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

    // save changes
    cfg_write();

    // send response
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

/* main functions */
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

void init(void)
{
    esp_err_t err;
    err = nvs_flash_init();
    if (err != ESP_OK)
    {
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
        {
            ESP_ERROR_CHECK(nvs_flash_erase());
            err = nvs_flash_init();
        }
        else
        {
            ESP_LOGE(TAG, "Error (%s) while initializing flash.", esp_err_to_name(err));
        }
    }

    cfg_init();
    cfg_read();
    gpio_init();
}

void app_main(void)
{
    init();
    enter_config_mode();
    while (1)
    {
        vTaskDelay(10000);
    }
}
