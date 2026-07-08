#pragma once

#include <GxEPD2_BW.h>
#include <gdey/GxEPD2_154_GDEY0154D67.h>
#include "epd_store.h"

class EpdBitmapDisplay {
public:
    using DisplayType = GxEPD2_BW<GxEPD2_154_GDEY0154D67, GxEPD2_154_GDEY0154D67::HEIGHT>;

    explicit EpdBitmapDisplay(DisplayType &display);

    void begin();
    void showBitmap(const uint8_t *bitmap, size_t len);
    void showText(const char *text);
    void showItem(const EpdItemMeta &meta, const uint8_t *data, size_t len);

private:
    DisplayType &display_;
    bool ready_ = false;
    void ensureReady();
    void remapSpi();

    static const SPISettings spiSettings_;
};
