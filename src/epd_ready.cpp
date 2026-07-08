#include "epd_ready.h"
#include "esp_log.h"
#include <Arduino.h>

static const char *TAG = "epd_rdy";

void epdReadyBegin()
{
#if EPD_READY_GPIO >= 0
    pinMode(EPD_READY_GPIO, OUTPUT);
    digitalWrite(EPD_READY_GPIO, LOW);
    ESP_LOGI(TAG, "READY gpio %d (low until first show)", EPD_READY_GPIO);
#endif
}

void epdReadySignalDone()
{
#if EPD_READY_GPIO >= 0
    digitalWrite(EPD_READY_GPIO, HIGH);
    ESP_LOGI(TAG, "READY gpio %d HIGH — screen ok, C3 may release S3 reset", EPD_READY_GPIO);
#endif
}
