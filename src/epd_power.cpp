#include "epd_power.h"
#include "epd_config.h"
#include "esp_log.h"
#include "esp_sleep.h"

static const char *TAG = "epd_pwr";

void epdPowerBegin()
{
    setCpuFrequencyMhz(EPD_CPU_MHZ);
    ESP_LOGI(TAG, "CPU %u MHz", (unsigned)EPD_CPU_MHZ);
}

void epdPowerIdle(uint32_t ms, bool allowLightSleep)
{
    if (ms == 0) {
        return;
    }
#if EPD_LIGHT_SLEEP_IDLE
    if (allowLightSleep && ms >= 50) {
        esp_sleep_enable_timer_wakeup((uint64_t)ms * 1000ULL);
        esp_light_sleep_start();
        return;
    }
#endif
    delay(ms);
}
