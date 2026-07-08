#pragma once

#include "epd_bitmap.h"
#include "epd_store.h"
#include <functional>

class EpdNetDisplay {
public:
    using ShowNotify = std::function<void()>;

    void begin(EpdStore &store, EpdBitmapDisplay &renderer, ShowNotify onShown);
    void loop();
    String ipAddress() const;

private:
    EpdStore *store_ = nullptr;
    EpdBitmapDisplay *renderer_ = nullptr;
    ShowNotify onShown_;
    void setupRoutes();
    void showItemById(const String &id);
};
