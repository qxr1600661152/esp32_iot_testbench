#include "epd_bitmap.h"
#include "epd_config.h"
#include "epd_formula.h"
#include "epd_pins.h"
#include "epd_store.h"
#include "esp_log.h"
#include <SPI.h>
#include <Fonts/FreeMonoBold9pt7b.h>

static const char *TAG = "epd_bm";

const SPISettings EpdBitmapDisplay::spiSettings_(4000000, MSBFIRST, SPI_MODE0);

EpdBitmapDisplay::EpdBitmapDisplay(DisplayType &display) : display_(display) {}

void EpdBitmapDisplay::remapSpi()
{
    SPI.end();
    if (EPD_MISO >= 0) {
        SPI.begin(EPD_SCK, EPD_MISO, EPD_MOSI, -1);
    } else {
        SPI.begin(EPD_SCK, -1, EPD_MOSI, -1);
    }
    display_.epd2.selectSPI(SPI, spiSettings_);
}

void EpdBitmapDisplay::begin()
{
    if (ready_) {
        return;
    }
    ESP_LOGI(TAG, "EPD pins BUSY=%d RST=%d DC=%d CS=%d SCK=%d MOSI=%d",
             EPD_BUSY, EPD_RST, EPD_DC, EPD_CS, EPD_SCK, EPD_MOSI);
    remapSpi();
    display_.init(115200, false, 50, true);
    remapSpi();
    ready_ = true;
    ESP_LOGI(TAG, "display ready %dx%d", EPD_WIDTH, EPD_HEIGHT);
}

void EpdBitmapDisplay::ensureReady()
{
    if (!ready_) {
        begin();
    }
}

void EpdBitmapDisplay::showBitmap(const uint8_t *bitmap, size_t len)
{
    ensureReady();
    if (len < EPD_BITMAP_BYTES) {
        ESP_LOGE(TAG, "bitmap too small: %u < %d", (unsigned)len, EPD_BITMAP_BYTES);
        return;
    }
    remapSpi();
    display_.epd2.writeImage(bitmap, 0, 0, EPD_WIDTH, EPD_HEIGHT, false, false, false);
    display_.epd2.refresh(false);
#if !EPD_SCREEN_TEST
    display_.hibernate();
    ESP_LOGI(TAG, "bitmap refreshed, panel sleep");
#else
    ESP_LOGI(TAG, "bitmap refreshed (screen test, no hibernate)");
#endif
}

void EpdBitmapDisplay::showText(const char *text)
{
    ensureReady();
    if (!text) {
        return;
    }
    String body = text;
    epdExpandFormulas(body);
    remapSpi();
    display_.setFullWindow();
    display_.firstPage();
    do {
        display_.fillScreen(GxEPD_WHITE);
        display_.setTextColor(GxEPD_BLACK);
        display_.setFont(&FreeMonoBold9pt7b);
        display_.setCursor(8, 24);
        display_.println(body.c_str());
    } while (display_.nextPage());
    display_.hibernate();
    ESP_LOGI(TAG, "text refreshed, panel sleep");
}

void EpdBitmapDisplay::showItem(const EpdItemMeta &meta, const uint8_t *data, size_t len)
{
    if (meta.type == EpdItemType::Text) {
        static char buf[EpdStore::MAX_TEXT_BYTES + 1];
        size_t n = min(len, (size_t)EpdStore::MAX_TEXT_BYTES);
        memcpy(buf, data, n);
        buf[n] = '\0';
        showText(buf);
    } else {
        showBitmap(data, len);
    }
}
