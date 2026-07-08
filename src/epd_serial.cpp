#include "epd_serial.h"
#include "epd_config.h"
#include "esp_log.h"

static const char *TAG = "epd_ser";
static const char MAGIC[] = "EPD1";

void EpdSerialDisplay::begin(BitmapHandler onBitmap)
{
    onBitmap_ = onBitmap;
    Serial.begin(115200);
    ESP_LOGI(TAG, "serial bitmap: send EPD1 + 5000 bytes");
}

void EpdSerialDisplay::loop()
{
    while (Serial.available()) {
        if (state_ == State::WaitMagic) {
            char c = Serial.read();
            lineBuf_ += c;
            if (lineBuf_.length() > 8) {
                lineBuf_.remove(0, 1);
            }
            if (lineBuf_.endsWith("\n")) {
                lineBuf_.trim();
                if (lineBuf_ == MAGIC) {
                    state_ = State::ReadBody;
                    bodyGot_ = 0;
                    ESP_LOGI(TAG, "magic ok, reading %d bytes", EPD_BITMAP_BYTES);
                }
                lineBuf_ = "";
            }
        } else {
            body_[bodyGot_++] = Serial.read();
            if (bodyGot_ >= EPD_BITMAP_BYTES) {
                if (onBitmap_) {
                    onBitmap_(body_, EPD_BITMAP_BYTES);
                }
                state_ = State::WaitMagic;
                bodyGot_ = 0;
                Serial.println("OK");
            }
        }
    }
}
