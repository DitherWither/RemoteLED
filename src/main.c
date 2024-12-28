#include <esp_log.h>
#include "network.h"
#include <esp_wifi_types_generic.h>
#include <esp_err.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include <esp_http_server.h>

#define TAG "main"
#define STRINGIFY(x) #x

const char *index_html =
#include "index.html"
    ;

esp_err_t index_get_handler(httpd_req_t *req)
{
    httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

httpd_uri_t index_get = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_get_handler,
    .user_ctx = NULL,
};

bool is_led_on = false;

esp_err_t toggle_get_handler(httpd_req_t *req)
{
    if (!is_led_on)
    {
        ESP_LOGI(TAG, "Turning on");
        gpio_set_level(2, 1);
        gpio_set_level(4, 1);
        is_led_on = true;
    }
    else
    {
        ESP_LOGI(TAG, "Turning off");
        gpio_set_level(2, 0);
        gpio_set_level(4, 0);
        is_led_on = false;
    }
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

httpd_uri_t toggle_get = {
    .uri = "/toggle",
    .method = HTTP_GET,
    .handler = toggle_get_handler,
    .user_ctx = NULL,
};

void app_main()
{
    gpio_reset_pin(2);
    gpio_set_direction(2, GPIO_MODE_OUTPUT);

    gpio_reset_pin(D);
    gpio_set_direction(4, GPIO_MODE_OUTPUT);

    ESP_LOGI("main", "Hello World!");

    ESP_ERROR_CHECK(network_init());
    ESP_ERROR_CHECK(network_connect("patil-wifi", "2dVrNLWF0DW3nM3l681P6bT"));

    wifi_ap_record_t ap_info;
    esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
    if (ret == ESP_ERR_WIFI_CONN)
    {
        ESP_LOGE(TAG, "Wi-Fi station interface not initialized");
    }
    else if (ret == ESP_ERR_WIFI_NOT_CONNECT)
    {
        ESP_LOGE(TAG, "Wi-Fi station is not connected");
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start webserver");
        return;
    }

    httpd_register_uri_handler(server, &index_get);
    httpd_register_uri_handler(server, &toggle_get);
}