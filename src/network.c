#include "network.h"
#include <nvs.h>
#include <nvs_flash.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <string.h>
#include <freertos/event_groups.h>

#define TAG "network"
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static const int WIFI_RETRY_ATTEMPT = 3;
static int wifi_retry_count = 0;

static EventGroupHandle_t wifi_event_group = NULL;
static esp_netif_t *netif = NULL;

static esp_event_handler_instance_t ip_event_handler;
static esp_event_handler_instance_t wifi_event_handler;

static void
ip_event_cb(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "Handling IP event, event code 0x%" PRIx32, event_id);
    switch (event_id)
    {
    case (IP_EVENT_STA_GOT_IP):
        ip_event_got_ip_t *event_ip = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event_ip->ip_info.ip));
        wifi_retry_count = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        break;
    case (IP_EVENT_STA_LOST_IP):
        ESP_LOGI(TAG, "Lost IP");
        break;
    case (IP_EVENT_GOT_IP6):
        ip_event_got_ip6_t *event_ip6 = (ip_event_got_ip6_t *)event_data;
        ESP_LOGI(TAG, "Got IPv6: " IPV6STR, IPV62STR(event_ip6->ip6_info.ip));
        wifi_retry_count = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        break;
    default:
        ESP_LOGI(TAG, "IP event not handled");
        break;
    }
}

static void wifi_event_cb(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "Handling Wi-Fi event, event code 0x%" PRIx32, event_id);

    switch (event_id)
    {
    case (WIFI_EVENT_WIFI_READY):
        ESP_LOGI(TAG, "Wi-Fi ready");
        break;
    case (WIFI_EVENT_SCAN_DONE):
        ESP_LOGI(TAG, "Wi-Fi scan done");
        break;
    case (WIFI_EVENT_STA_START):
        ESP_LOGI(TAG, "Wi-Fi started, connecting to AP...");
        esp_wifi_connect();
        break;
    case (WIFI_EVENT_STA_STOP):
        ESP_LOGI(TAG, "Wi-Fi stopped");
        break;
    case (WIFI_EVENT_STA_CONNECTED):
        ESP_LOGI(TAG, "Wi-Fi connected");
        break;
    case (WIFI_EVENT_STA_DISCONNECTED):
        ESP_LOGI(TAG, "Wi-Fi disconnected");
        if (wifi_retry_count < WIFI_RETRY_ATTEMPT)
        {
            ESP_LOGI(TAG, "Retrying to connect to Wi-Fi network...");
            esp_wifi_connect();
            wifi_retry_count++;
        }
        else
        {
            ESP_LOGI(TAG, "Failed to connect to Wi-Fi network");
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        }
        break;
    case (WIFI_EVENT_STA_AUTHMODE_CHANGE):
        ESP_LOGI(TAG, "Wi-Fi authmode changed");
        break;
    default:
        ESP_LOGI(TAG, "Wi-Fi event not handled");
        break;
    }
}

esp_err_t network_init()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGI(TAG, "Erasing flash");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK)
    {
        return ret;
    }

    wifi_event_group = xEventGroupCreate();

    ret = esp_netif_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize TCP/IP network stack");
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create default event loop");
        return ret;
    }

    ret = esp_wifi_set_default_wifi_sta_handlers();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set default WiFi handlers");
        return ret;
    }

    netif = esp_netif_create_default_wifi_sta();
    if (netif == NULL)
    {
        ESP_LOGE(TAG, "Failed to create WiFi STA interface");
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_cb,
                                                        NULL,
                                                        &wifi_event_handler));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &ip_event_cb,
                                                        NULL,
                                                        &ip_event_handler));

    return ESP_OK;
}

esp_err_t network_connect(char *ssid, char *password)
{
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    ESP_LOGI(TAG, "Connecting to WiFi network %s", ssid);
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "Connected to Wi-Fi network: %s", wifi_config.sta.ssid);
        return ESP_OK;
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGE(TAG, "Failed to connect to Wi-Fi network: %s", wifi_config.sta.ssid);
        return ESP_FAIL;
    }

    ESP_LOGE(TAG, "Unexpected Wi-Fi error");
    return ESP_FAIL;
}