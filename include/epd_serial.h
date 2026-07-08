#pragma once

#include "epd_config.h"
#include <Arduino.h>
#include <functional>

using BitmapHandler = std::function<void(const uint8_t *, size_t)>;

class EpdSerialDisplay {
public:
    void begin(BitmapHandler onBitmap);
    void loop();

private:
    BitmapHandler onBitmap_;
    enum class State { WaitMagic, ReadBody };
    State state_ = State::WaitMagic;
    String lineBuf_;
    size_t bodyGot_ = 0;
    uint8_t body_[EPD_BITMAP_BYTES];
};
