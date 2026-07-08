
#define CONFIG_BT_NIMBLE_NVS_PERSIST 0
#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <gdey/GxEPD2_154_GDEY0154D67.h>
#include "demo_bitmaps.h"
#include "epd_bitmap.h"
#include "epd_ble.h"
#include "epd_config.h"
#include "epd_net.h"
#include "epd_pins.h"
#include "epd_power.h"
#include "epd_ready.h"
#include "epd_serial.h"
#include "epd_store.h"
#include "esp_log.h"
#if EPD_BLE_ENABLE && EPD_BLE_STANDALONE
#include <WiFi.h>
#endif

static const char *TAG = "main";

static GxEPD2_BW<GxEPD2_154_GDEY0154D67, GxEPD2_154_GDEY0154D67::HEIGHT> epd(
    GxEPD2_154_GDEY0154D67(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

static EpdBitmapDisplay renderer(epd);
static EpdStore store;
static EpdNetDisplay netDisplay;
static EpdSerialDisplay serialDisplay;
static EpdBle bleDisplay;

static void onContentShown()
{
    epdReadySignalDone();
}

static void showHandler(const EpdItemMeta &m, const uint8_t *d, size_t n)
{
    renderer.showItem(m, d, n);
    onContentShown();
}

static void showDefaultLogo()
{
    if (store.count() > 0) {
        store.showByIndex(0, showHandler);
        ESP_LOGI(TAG, "show #0 from store");
        return;
    }
    uint8_t logo[EPD_BITMAP_BYTES];
    memcpy_P(logo, DEMO_LOGO, EPD_BITMAP_BYTES);
    renderer.showBitmap(logo, EPD_BITMAP_BYTES);
    onContentShown();
    ESP_LOGI(TAG, "default logo shown");
}

#if EPD_BLE_ENABLE
static String gSerialCmd;

static void pollBleSerialCommands()
{
    while (Serial.available()) {
        if (gSerialCmd.isEmpty() && Serial.peek() == 'E') {
            return;
        }
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            gSerialCmd.trim();
            if (gSerialCmd == "BLE_OPEN") {
                bleDisplay.openConnectWindow(60000);
            } else if (gSerialCmd == "BLE_STATUS") {
                ESP_LOGI(TAG, "BLE %s connected=%d", EPD_BLE_NAME, bleDisplay.isConnected());
            }
            gSerialCmd = "";
        } else if (gSerialCmd.length() < 24) {
            gSerialCmd += c;
        }
    }
}
#endif

#if EPD_SCREEN_TEST
static size_t gTestFrameIdx = 0;
static uint32_t gTestNextMs = 0;

static void showTestFrame(size_t idx)
{
    const size_t n = idx % DEMO_FRAME_COUNT;
    static uint8_t buf[EPD_BITMAP_BYTES];
    const uint8_t *src = (const uint8_t *)pgm_read_ptr(&DEMO_FRAMES[n]);
    memcpy_P(buf, src, EPD_BITMAP_BYTES);
    ESP_LOGI(TAG, "SCREEN TEST [%u/%u] %s", (unsigned)(n + 1), (unsigned)DEMO_FRAME_COUNT,
             DEMO_FRAME_NAMES[n]);
    Serial.printf("[s3] screen frame %u/%u %s\n", (unsigned)(n + 1), (unsigned)DEMO_FRAME_COUNT,
                  DEMO_FRAME_NAMES[n]);
    renderer.showBitmap(buf, EPD_BITMAP_BYTES);
    epdReadySignalDone();
}

static void runScreenTestSetup()
{
    ESP_LOGI(TAG, "=== SCREEN WIRE TEST: %u frames, %ums gap ===",
             (unsigned)DEMO_FRAME_COUNT, (unsigned)EPD_SCREEN_TEST_INTERVAL_MS);
    Serial.println("[s3] SCREEN WIRE TEST begin");
    renderer.begin();
    epdReadyBegin();
    showTestFrame(0);
    gTestFrameIdx = 1;
    gTestNextMs = millis() + EPD_SCREEN_TEST_INTERVAL_MS;
}

static void runScreenTestLoop()
{
    if (millis() < gTestNextMs) {
        delay(10);
        return;
    }
    showTestFrame(gTestFrameIdx++);
    gTestNextMs = millis() + EPD_SCREEN_TEST_INTERVAL_MS;
}
#endif

static bool gPendingBootLogo = false;
static uint32_t gBootLogoEarliestMs = 0;
#if EPD_BLE_ENABLE
static bool gBleStarted = false;
#endif

void setup()
{
    delay(EPD_BOOT_DELAY_MS);
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("[s3] setup start");
    Serial.flush();
    epdPowerBegin();
    ESP_LOGI(TAG, "espviewproj boot target=%s", EPD_BLE_NAME);

#if EPD_SCREEN_TEST
    runScreenTestSetup();
    return;
#endif

    epdReadyBegin();

    if (!store.begin()) {
        ESP_LOGE(TAG, "store init failed");
    }
    store.importDemoAssets();
    Serial.println("[s3] store ok");
    Serial.flush();

    serialDisplay.begin([](const uint8_t *data, size_t len) {
        renderer.showBitmap(data, len);
        onContentShown();
    });

#if EPD_BLE_ENABLE

#endif

#if !(EPD_SKIP_BOOT_EPD)
    gPendingBootLogo = true;
    gBootLogoEarliestMs = millis() + 3000;
#endif

#if !(EPD_BLE_ENABLE && EPD_BLE_STANDALONE)
    netDisplay.begin(store, renderer, onContentShown);
#else
    ESP_LOGI(TAG, "BLE standalone: WiFi AP skipped");
#endif

    ESP_LOGI(TAG, "BLE name: %s", EPD_BLE_NAME);
#if EPD_DUAL_CORE_BLE
    ESP_LOGI(TAG, "dual-core: BLE core=%d display core=%d", EPD_BLE_CORE, EPD_DISPLAY_CORE);
#endif
#if !(EPD_BLE_ENABLE && EPD_BLE_STANDALONE)
    ESP_LOGI(TAG, "WiFi http:
#endif
}

void loop()
{
#if EPD_SCREEN_TEST
    runScreenTestLoop();
    return;
#endif

#if EPD_BLE_ENABLE
    if (!gBleStarted) {
        gBleStarted = true;
        Serial.println("[s3] ble init deferred...");
        Serial.flush();
        bleDisplay.begin(store, renderer, onContentShown);
        Serial.println("[s3] ble begin done");
        Serial.flush();
    }
#endif

#if !(EPD_SKIP_BOOT_EPD)
    if (gPendingBootLogo && bleDisplay.isConnected()) {
        gPendingBootLogo = false;
        showDefaultLogo();
    }
#endif

#if !(EPD_BLE_ENABLE && EPD_BLE_STANDALONE)
    netDisplay.loop();
#endif
    serialDisplay.loop();
#if EPD_BLE_ENABLE
    bleDisplay.loop();
    pollBleSerialCommands();
#endif

    uint32_t idleMs = EPD_IDLE_LOOP_MS_IDLE;
    bool lightOk = true;
#if EPD_BLE_ENABLE
    if (!bleDisplay.isConnected()) {
        idleMs = EPD_IDLE_LOOP_MS_PAIRING;
        lightOk = false;
    } else if (bleDisplay.wantsFastLoop()) {
        idleMs = EPD_IDLE_LOOP_MS_CONNECTED;
        lightOk = false;
    } else {
        lightOk = bleDisplay.allowLightSleep();
    }
#endif
    epdPowerIdle(idleMs, lightOk);
}
