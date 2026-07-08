#pragma once

#include "epd_config.h"
#include "epd_bitmap.h"
#include "epd_store.h"
#include <Arduino.h>
#if EPD_DUAL_CORE_BLE
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

class EpdBle {
public:
    using ShowNotify = std::function<void()>;

    void begin(EpdStore &store, EpdBitmapDisplay &renderer, ShowNotify onShown);
    void loop();
#if EPD_DUAL_CORE_BLE
    void startBleCoreTask();
    void bleServiceLoop();
    void displayWorkLoop();
#endif

    bool isConnected() const { return connected_; }

    bool isDiscoverableWindow() const { return discoverableWindow_; }
    bool wantsFastLoop() const
    {
        return connected_ || epdBusy_ || pendingShow_ || pendingFinishPut_;
    }
    bool allowLightSleep() const
    {

        if (!connected_) {
            return false;
        }
        return !wantsFastLoop();
    }

    void openConnectWindow(uint32_t durationMs = 60000);

    void onClientConnect(uint16_t connHandle);
    void onClientDisconnect();
    void onGattReady();
    void onNotifySubscribed();
    void onRx(const uint8_t *data, size_t len);

private:
    enum class RxMode { Line, Binary, HexLine };
    enum class PendingShowKind : uint8_t { None, ById, Next, Prev, Goto };

    EpdStore *store_ = nullptr;
    EpdBitmapDisplay *renderer_ = nullptr;
    ShowNotify onShown_;

    String lineBuf_;
    RxMode rxMode_ = RxMode::Line;

    bool connected_ = false;
    bool discoverableWindow_ = true;
    bool gattReady_ = false;
    bool hostReady_ = false;
    bool autoConnectSuspect_ = false;
    bool connParamsDone_ = false;
    uint32_t ghostCooldownUntilMs_ = 0;
    bool epdBusy_ = false;
    uint16_t connHandle_ = 0xFFFF;

    uint32_t discoverableUntilMs_ = 0;
    uint32_t connectAtMs_ = 0;
    uint32_t lastAdvKickMs_ = 0;
    uint32_t lastHostPollMs_ = 0;
    uint32_t bootMs_ = 0;

    bool pendingShow_ = false;
    PendingShowKind pendingShowKind_ = PendingShowKind::None;
    String pendingShowId_;
    uint16_t pendingGotoIdx_ = 0;
    bool pendingFinishPut_ = false;

    String putName_;
    EpdItemType putType_ = EpdItemType::Bitmap;
    uint8_t rxBuf_[EPD_BITMAP_BYTES];
    size_t rxNeed_ = 0;
    size_t rxGot_ = 0;
    uint32_t rxUploadLastMs_ = 0;

    void resetRxUpload();

    void initStack();
    void applyAdvTiming();
    void ensureAdvertising(const char *reason);
    void touchSessionActivity();
    void maintainConnection();
    void disconnectStalePeers(const char *reason);
    void updateConnParams(uint16_t connHandle);

    void notifyText(const String &s);
    void handleLine(const String &line);
    void appendHexLine(const String &hex);
    void queueFinishPut();
    void runFinishPut();
    void queueShow(PendingShowKind kind, const String &id = "", uint16_t idx = 0);
    void runShow();

#if EPD_DUAL_CORE_BLE
    TaskHandle_t bleTask_ = nullptr;
    static void bleCoreTaskEntry(void *arg);
#endif
};
