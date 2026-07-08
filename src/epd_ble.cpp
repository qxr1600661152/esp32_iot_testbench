#include "epd_ble.h"
#include "epd_config.h"
#include "epd_store.h"
#include "esp_log.h"
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <vector>
#if EPD_DUAL_CORE_BLE
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

static const char *TAG = "epd_ble";

static bool isBleCommandLine(const String &line);
static const char *NUS_SERVICE = EPD_BLE_SVC;
static const char *NUS_RX = EPD_BLE_RX;
static const char *NUS_TX = EPD_BLE_TX;

static NimBLECharacteristic *gTxChar = nullptr;
static NimBLEServer *gServer = nullptr;
static EpdBle *gBle = nullptr;

static void bleNotifyChunked(const String &msg)
{
    if (!gTxChar) {
        return;
    }
    const size_t mtu = NimBLEDevice::getMTU();
    const size_t chunk = (mtu > 23) ? (mtu - 3) : 20;
    for (size_t i = 0; i < msg.length(); i += chunk) {
        size_t n = msg.length() - i;
        if (n > chunk) {
            n = chunk;
        }
        gTxChar->setValue((uint8_t *)(msg.c_str() + i), n);
        gTxChar->notify();
        delay(2);
    }
}

class RxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pCharacteristic) override
    {
        if (!gBle) {
            return;
        }
        std::string v = pCharacteristic->getValue();
        if (v.empty()) {
            return;
        }
        gBle->onRx((const uint8_t *)v.data(), v.size());
    }
};

class TxCallbacks : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic *, ble_gap_conn_desc *, uint16_t subValue) override
    {
        (void)subValue;
        if (gBle) {
            gBle->onNotifySubscribed();
        }
    }
};

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *pServer, ble_gap_conn_desc *desc) override
    {
        Serial.println("[s3] BLE onConnect");
        Serial.flush();
        if (desc) {
            NimBLEAddress peer(desc->peer_ota_addr.val);
            ESP_LOGI(TAG, "[BLE] CONNECT handle=%u peer=%s", (unsigned)desc->conn_handle,
                     peer.toString().c_str());
            if (gBle) {
                gBle->onClientConnect(desc->conn_handle);
            }
        }
    }

    void onMTUChange(uint16_t mtu, ble_gap_conn_desc *desc) override
    {
        (void)mtu;
        (void)desc;
        if (gBle) {
            gBle->onGattReady();
        }
    }

    void onDisconnect(NimBLEServer *) override
    {
        Serial.println("[s3] BLE onDisconnect");
        Serial.flush();
        if (gBle) {
            gBle->onClientDisconnect();
        }
    }
};

void EpdBle::applyAdvTiming()
{
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    if (!adv) {
        return;
    }
    if (!connected_) {
        adv->setMinInterval(EPD_BLE_ADV_MIN_CONNECTABLE);
        adv->setMaxInterval(EPD_BLE_ADV_MAX_CONNECTABLE);
    }
}

void EpdBle::ensureAdvertising(const char *reason)
{
    if (connected_) {
        return;
    }
#if EPD_BLE_ADV_OFF_WHEN_IDLE
    if (!discoverableWindow_) {
        NimBLEDevice::stopAdvertising();
        ESP_LOGI(TAG, "[ADV] OFF (%s)", reason);
        return;
    }
#endif
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    if (!adv) {
        return;
    }
#if EPD_BLE_ALWAYS_CONNECTABLE
    adv->setAdvertisementType(BLE_GAP_CONN_MODE_UND);
#endif
    applyAdvTiming();
    if (adv->isAdvertising()) {
        lastAdvKickMs_ = millis();
        return;
    }
    if (!NimBLEDevice::startAdvertising()) {
        ESP_LOGW(TAG, "[ADV] start failed (%s)", reason);
        NimBLEDevice::stopAdvertising();
        delay(20);
        if (!NimBLEDevice::startAdvertising()) {
            ESP_LOGW(TAG, "[ADV] retry failed (%s)", reason);
        }
    }
    lastAdvKickMs_ = millis();
    ESP_LOGI(TAG, "[ADV] connectable (%s)", reason);
}

void EpdBle::onClientConnect(uint16_t connHandle)
{
    if ((millis() - bootMs_) < EPD_BLE_BOOT_AUTOCONNECT_MS) {
        ESP_LOGW(TAG, "[BLE] reject boot-window connect");
        if (gServer) {
            gServer->disconnect(connHandle);
        }
        return;
    }
    connected_ = true;
    connHandle_ = connHandle;
    connectAtMs_ = millis();
    lastHostPollMs_ = millis();
    gattReady_ = true;
    hostReady_ = false;
    connParamsDone_ = false;
    touchSessionActivity();
}

void EpdBle::onNotifySubscribed()
{
    hostReady_ = true;
    ESP_LOGI(TAG, "[BLE] notify subscribed");
}

void EpdBle::touchSessionActivity()
{
    discoverableWindow_ = true;
    discoverableUntilMs_ = millis() + 300000;
}

void EpdBle::disconnectStalePeers(const char *reason)
{
    if (!gServer || gServer->getConnectedCount() == 0) {
        return;
    }
    const std::vector<uint16_t> handles = gServer->getPeerDevices();
    for (uint16_t h : handles) {
        gServer->disconnect(h);
    }
    connected_ = false;
    hostReady_ = false;
    connParamsDone_ = false;
    connHandle_ = 0xFFFF;
    ESP_LOGW(TAG, "[BLE] dropped stale peers (%s)", reason);
    ensureAdvertising("stale-drop");
}

void EpdBle::maintainConnection()
{
#if EPD_BLE_HOST_POLL_MS > 0
    if (connected_ && gattReady_ && hostReady_ &&
        millis() - connectAtMs_ >= EPD_BLE_CONNECT_GRACE_MS &&
        millis() - lastHostPollMs_ >= EPD_BLE_HOST_POLL_MS) {
        lastHostPollMs_ = millis();
        if (!epdBusy_ && !pendingShow_ && !pendingFinishPut_ && rxMode_ == RxMode::Line) {
            notifyText("POLL\n");
        }
    }
#endif
}

void EpdBle::updateConnParams(uint16_t connHandle)
{
    if (!gServer) {
        return;
    }

    gServer->updateConnParams(connHandle, 24, 48, 0, 8000);
}

void EpdBle::onGattReady()
{
    gattReady_ = true;
    ESP_LOGI(TAG, "[BLE] GATT ready");
}

void EpdBle::onClientDisconnect()
{
    const uint32_t sessionMs = connected_ ? (millis() - connectAtMs_) : 0;
    connected_ = false;
    connHandle_ = 0xFFFF;
    gattReady_ = false;
    hostReady_ = false;
    connParamsDone_ = false;
    pendingShow_ = false;
    pendingFinishPut_ = false;
    epdBusy_ = false;
    resetRxUpload();
    ESP_LOGI(TAG, "[BLE] DISCONNECT session=%ums", (unsigned)sessionMs);
    ensureAdvertising("onDisconnect");
}

void EpdBle::initStack()
{
#if EPD_BLE_STANDALONE
    WiFi.mode(WIFI_OFF);
    delay(50);
#endif

    ESP_LOGI(TAG, "NimBLE init heap=%u", (unsigned)ESP.getFreeHeap());
    NimBLEDevice::init(EPD_BLE_NAME);
#if EPD_BLE_CLEAR_BONDS_ON_BOOT
    NimBLEDevice::deleteAllBonds();
#endif
    NimBLEDevice::setPower(EPD_BLE_TX_LEVEL);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setSecurityAuth(false, false, false);

    gServer = NimBLEDevice::createServer();
    gServer->setCallbacks(new ServerCallbacks());
    gServer->advertiseOnDisconnect(true);

    NimBLEService *svc = gServer->createService(NUS_SERVICE);
    NimBLECharacteristic *rx = svc->createCharacteristic(
        NUS_RX, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    rx->setCallbacks(new RxCallbacks());
    gTxChar = svc->createCharacteristic(
        NUS_TX, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);
    gTxChar->setCallbacks(new TxCallbacks());
    svc->start();

    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(NUS_SERVICE);
    adv->setScanResponse(true);
    adv->setName(EPD_BLE_NAME);
    adv->setMinInterval(EPD_BLE_ADV_MIN_CONNECTABLE);
    adv->setMaxInterval(EPD_BLE_ADV_MAX_CONNECTABLE);
    delay(300);
    ESP_LOGI(TAG, "NimBLE %s addr=%s", EPD_BLE_NAME, NimBLEDevice::getAddress().toString().c_str());
    Serial.printf("[s3] BLE addr %s\n", NimBLEDevice::getAddress().toString().c_str());
}

void EpdBle::begin(EpdStore &store, EpdBitmapDisplay &renderer, ShowNotify onShown)
{
    store_ = &store;
    renderer_ = &renderer;
    onShown_ = onShown;
    gBle = this;
    bootMs_ = millis();
    discoverableWindow_ = true;
    discoverableUntilMs_ = millis() + EPD_BLE_BOOT_DISCOVER_MS;
    initStack();
    ensureAdvertising("boot");
    const bool advOn = NimBLEDevice::getAdvertising() && NimBLEDevice::getAdvertising()->isAdvertising();
    const unsigned peerCnt = gServer ? gServer->getConnectedCount() : 0;
    Serial.printf("[s3] BLE adv=%d peers=%u\n", advOn ? 1 : 0, peerCnt);
    ESP_LOGI(TAG, "[BLE] always connectable when idle (no bond/pin)");
    Serial.printf("[s3] BLE ready — %s (always connectable)\n", EPD_BLE_NAME);
#if EPD_DUAL_CORE_BLE
    startBleCoreTask();
#endif
}

#if EPD_DUAL_CORE_BLE
void EpdBle::bleCoreTaskEntry(void *arg)
{
    auto *self = static_cast<EpdBle *>(arg);
    ESP_LOGI(TAG, "BLE service task on core %d", xPortGetCoreID());
    for (;;) {
        self->bleServiceLoop();
        vTaskDelay(pdMS_TO_TICKS(EPD_IDLE_LOOP_MS_CONNECTED));
    }
}

void EpdBle::startBleCoreTask()
{
    if (bleTask_) {
        return;
    }
    xTaskCreatePinnedToCore(
        bleCoreTaskEntry, "epd_ble", 12288, this, 5, &bleTask_, EPD_BLE_CORE);
}

void EpdBle::bleServiceLoop()
{
    if (discoverableWindow_ && !connected_ && millis() >= discoverableUntilMs_) {
        discoverableWindow_ = false;
        ESP_LOGI(TAG, "[BLE] discover window ended (adv stays connectable)");
    }
    if (!connected_ && millis() - lastAdvKickMs_ >= EPD_BLE_ADV_WATCHDOG_MS) {
        ensureAdvertising("watchdog");
    }
#if EPD_BLE_UPLOAD_TIMEOUT_MS > 0
    if (rxMode_ == RxMode::HexLine && rxNeed_ > 0 && rxUploadLastMs_ > 0 &&
        millis() - rxUploadLastMs_ >= EPD_BLE_UPLOAD_TIMEOUT_MS) {
        ESP_LOGW(TAG, "[BLE] upload timeout %u/%u", (unsigned)rxGot_, (unsigned)rxNeed_);
        resetRxUpload();
        if (connected_) {
            notifyText("ERR upload timeout\n");
        }
    }
#endif
#if EPD_BLE_HOST_POLL_MS > 0
    maintainConnection();
#endif
}

void EpdBle::displayWorkLoop()
{
    if (pendingFinishPut_) {
        pendingFinishPut_ = false;
        runFinishPut();
    }
    if (pendingShow_) {
        runShow();
    }
}

void EpdBle::loop()
{
    displayWorkLoop();
}
#else
void EpdBle::loop()
{
    if (discoverableWindow_ && !connected_ && millis() >= discoverableUntilMs_) {
        discoverableWindow_ = false;
        ESP_LOGI(TAG, "[BLE] discover window ended (adv stays connectable)");
    }
    if (!connected_ && millis() - lastAdvKickMs_ >= EPD_BLE_ADV_WATCHDOG_MS) {
        ensureAdvertising("watchdog");
    }
#if EPD_BLE_UPLOAD_TIMEOUT_MS > 0
    if (rxMode_ == RxMode::HexLine && rxNeed_ > 0 && rxUploadLastMs_ > 0 &&
        millis() - rxUploadLastMs_ >= EPD_BLE_UPLOAD_TIMEOUT_MS) {
        ESP_LOGW(TAG, "[BLE] upload timeout %u/%u", (unsigned)rxGot_, (unsigned)rxNeed_);
        resetRxUpload();
        if (connected_) {
            notifyText("ERR upload timeout\n");
        }
    }
#endif
    if (pendingFinishPut_) {
        pendingFinishPut_ = false;
        runFinishPut();
    }
    if (pendingShow_) {
        pendingShow_ = false;
        runShow();
    }
    maintainConnection();
}
#endif

void EpdBle::openConnectWindow(uint32_t durationMs)
{
    discoverableWindow_ = true;
    discoverableUntilMs_ = millis() + durationMs;
    if (connected_ && gattReady_) {
        ESP_LOGI(TAG, "[BLE] discover window extended (connected)");
        return;
    }
    NimBLEDevice::stopAdvertising();
    delay(20);
    ensureAdvertising("BLE_OPEN");
    ESP_LOGI(TAG, "[BLE] discover OPEN %us", (unsigned)(durationMs / 1000));
    Serial.printf("[s3] BLE discover +%us\n", (unsigned)(durationMs / 1000));
}

void EpdBle::resetRxUpload()
{
    rxMode_ = RxMode::Line;
    rxGot_ = 0;
    rxNeed_ = 0;
    lineBuf_ = "";
    pendingFinishPut_ = false;
    rxUploadLastMs_ = 0;
}

void EpdBle::notifyText(const String &s)
{
    bleNotifyChunked(s);
}

void EpdBle::queueShow(PendingShowKind kind, const String &id, uint16_t idx)
{
    if (epdBusy_ || pendingShow_) {
        return;
    }
    pendingShowKind_ = kind;
    pendingShowId_ = id;
    pendingGotoIdx_ = idx;
    pendingShow_ = true;
}

void EpdBle::runShow()
{
    if (!store_ || !renderer_ || !pendingShow_) {
        return;
    }
    pendingShow_ = false;
    PendingShowKind kind = pendingShowKind_;
    pendingShowKind_ = PendingShowKind::None;

    epdBusy_ = true;
    if (connected_) {
        notifyText("OK showing start\n");
    }
    auto showFn = [this](const EpdItemMeta &m, const uint8_t *d, size_t n) {
        renderer_->showItem(m, d, n);
        if (onShown_) {
            onShown_();
        }
    };

    bool ok = false;
    switch (kind) {
    case PendingShowKind::ById:
        ok = store_->show(pendingShowId_, showFn);
        break;
    case PendingShowKind::Next:
        ok = store_->showNext(showFn);
        break;
    case PendingShowKind::Prev:
        ok = store_->showPrev(showFn);
        break;
    case PendingShowKind::Goto:
        ok = store_->showByIndex(pendingGotoIdx_, showFn);
        break;
    default:
        break;
    }
    epdBusy_ = false;

    if (ok && connected_) {
        notifyText("OK showing " + String(store_->currentIndex()) + "\n");
    } else if (connected_) {
        notifyText("ERR show failed\n");
    }
}

void EpdBle::onRx(const uint8_t *data, size_t len)
{
    hostReady_ = true;
    gattReady_ = true;
    touchSessionActivity();
    for (size_t i = 0; i < len; ++i) {
        char c = (char)data[i];
        if (rxMode_ == RxMode::Binary) {
            if (rxGot_ < rxNeed_) {
                rxBuf_[rxGot_++] = (uint8_t)c;
            }
            if (rxGot_ >= rxNeed_) {
                queueFinishPut();
            }
            continue;
        }

        if (c == '\n' || c == '\r') {
            if (lineBuf_.length() > 0) {
                if (rxMode_ == RxMode::HexLine) {
                    if (lineBuf_ == "ABORT") {
                        resetRxUpload();
                        notifyText("OK aborted\n");
                        lineBuf_ = "";
                        continue;
                    }
                    if (isBleCommandLine(lineBuf_)) {
                        if (rxGot_ > 0) {
                            notifyText("ERR upload in progress\n");
                        } else {
                            resetRxUpload();
                            handleLine(lineBuf_);
                        }
                        lineBuf_ = "";
                        continue;
                    }
                    appendHexLine(lineBuf_);
                    lineBuf_ = "";
                    rxUploadLastMs_ = millis();
                    if (rxGot_ >= rxNeed_) {
                        queueFinishPut();
                    }
                } else {
                    handleLine(lineBuf_);
                    lineBuf_ = "";
                }
            }
        } else if (lineBuf_.length() < 240) {
            lineBuf_ += c;
            if (rxMode_ == RxMode::HexLine) {
                rxUploadLastMs_ = millis();
            }
        }
    }
}

static int hexVal(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static bool isBleCommandLine(const String &line)
{
    return line == "LIST" || line == "STATE" || line == "NEXT" || line == "PREV" ||
           line == "ABORT" || line == "BLE_OPEN" || line.startsWith("PUT ") || line.startsWith("PUTTEXT ") ||
           line.startsWith("SHOW ") || line.startsWith("DEL ") || line.startsWith("GOTO ");
}

void EpdBle::appendHexLine(const String &hex)
{
    for (size_t i = 0; i + 1 < hex.length() && rxGot_ < rxNeed_; i += 2) {
        int hi = hexVal(hex[i]);
        int lo = hexVal(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            continue;
        }
        rxBuf_[rxGot_++] = (uint8_t)((hi << 4) | lo);
    }
}

void EpdBle::handleLine(const String &line)
{
    if (!store_) {
        notifyText("ERR no store\n");
        return;
    }

    if (line == "ABORT") {
        resetRxUpload();
        notifyText("OK aborted\n");
        return;
    }

    if (line == "BLE_OPEN") {
        openConnectWindow(60000);
        notifyText("OK discover 60s\n");
        return;
    }

    if (line == "LIST" || line == "STATE") {
        String js;
        store_->listJson(js);
        notifyText("OK " + js + "\n");
        return;
    }

    if (line == "NEXT") {
        if (epdBusy_ || pendingShow_) {
            notifyText("ERR busy\n");
            return;
        }
        queueShow(PendingShowKind::Next);
        notifyText("OK queued\n");
        return;
    }

    if (line == "PREV") {
        if (epdBusy_ || pendingShow_) {
            notifyText("ERR busy\n");
            return;
        }
        queueShow(PendingShowKind::Prev);
        notifyText("OK queued\n");
        return;
    }

    if (line.startsWith("GOTO ")) {
        int idx = line.substring(5).toInt();
        if (idx < 0) {
            notifyText("ERR bad index\n");
            return;
        }
        if (epdBusy_ || pendingShow_) {
            notifyText("ERR busy\n");
            return;
        }
        queueShow(PendingShowKind::Goto, "", (uint16_t)idx);
        notifyText("OK queued\n");
        return;
    }

    if (line.startsWith("PUTTEXT ")) {
        int sp = line.indexOf(' ', 8);
        if (sp < 0) {
            notifyText("ERR usage PUTTEXT name text\n");
            return;
        }
        String name = line.substring(8, sp);
        String text = line.substring(sp + 1);
        if (text.length() == 0 || text.length() > EpdStore::MAX_TEXT_BYTES) {
            notifyText("ERR text size\n");
            return;
        }
        String id;
        if (store_->create(name, EpdItemType::Text, (const uint8_t *)text.c_str(), text.length(), id)) {
            notifyText("OK " + id + "\n");
        } else {
            notifyText("ERR put failed\n");
        }
        return;
    }

    if (line.startsWith("DEL ")) {
        String id = line.substring(4);
        id.trim();
        if (store_->remove(id)) {
            String js;
            store_->listJson(js);
            notifyText("OK deleted " + js + "\n");
        } else {
            notifyText("ERR not found\n");
        }
        return;
    }

    if (line.startsWith("SHOW ")) {
        String id = line.substring(5);
        id.trim();
        EpdItemMeta meta;
        if (!store_->findById(id, meta)) {
            notifyText("ERR not found\n");
            return;
        }
        if (epdBusy_ || pendingShow_) {
            notifyText("ERR busy\n");
            return;
        }
        queueShow(PendingShowKind::ById, id);
        notifyText("OK queued\n");
        return;
    }

    if (line.startsWith("PUT ")) {
        if (rxMode_ == RxMode::HexLine && rxNeed_ > 0) {
            notifyText("ERR upload in progress\n");
            return;
        }
        if (epdBusy_ || pendingShow_ || pendingFinishPut_) {
            notifyText("ERR busy\n");
            return;
        }
        int p1 = line.indexOf(' ', 4);
        int p2 = (p1 > 0) ? line.indexOf(' ', p1 + 1) : -1;
        if (p1 < 0 || p2 < 0) {
            notifyText("ERR usage PUT name type size\n");
            return;
        }
        putName_ = line.substring(4, p1);
        String type = line.substring(p1 + 1, p2);
        putType_ = (type == "text") ? EpdItemType::Text : EpdItemType::Bitmap;
        rxNeed_ = (size_t)line.substring(p2 + 1).toInt();
        if (putType_ == EpdItemType::Bitmap && rxNeed_ != EPD_BITMAP_BYTES) {
            notifyText("ERR bitmap size\n");
            return;
        }
        if (rxNeed_ == 0 || rxNeed_ > sizeof(rxBuf_)) {
            notifyText("ERR size\n");
            return;
        }
        rxGot_ = 0;
        rxMode_ = RxMode::HexLine;
        rxUploadLastMs_ = millis();
        notifyText("READY\n");
        return;
    }

    notifyText("ERR unknown cmd\n");
}

void EpdBle::queueFinishPut()
{
    rxMode_ = RxMode::Line;
    pendingFinishPut_ = true;
    rxUploadLastMs_ = 0;
}

void EpdBle::runFinishPut()
{
    String id;
    if (!store_) {
        notifyText("ERR no store\n");
        rxGot_ = 0;
        rxNeed_ = 0;
        return;
    }
    if (rxGot_ != rxNeed_) {
        notifyText("ERR incomplete " + String(rxGot_) + "/" + String(rxNeed_) + "\n");
        rxGot_ = 0;
        rxNeed_ = 0;
        return;
    }
    if (store_->create(putName_, putType_, rxBuf_, rxGot_, id)) {
        notifyText("OK " + id + "\n");
    } else {
        notifyText("ERR put failed\n");
    }
    rxGot_ = 0;
    rxNeed_ = 0;
}
