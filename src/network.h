#include <esp_err.h>

esp_err_t network_init();

esp_err_t network_connect(char *ssid, char *password);