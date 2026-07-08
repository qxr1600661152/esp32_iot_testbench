#include "epd_net.h"
#include "epd_config.h"
#include "esp_log.h"
#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFi.h>
#include "web_assets.h"

static const char *TAG = "epd_net";
static WebServer server(EPD_HTTP_PORT);
static EpdNetDisplay *gNet = nullptr;
static EpdStore *gStore = nullptr;
static EpdBitmapDisplay *gRenderer = nullptr;
static uint8_t bodyBuf[EPD_BITMAP_BYTES];

static void addCors()
{
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

static void sendJson(int code, const String &body)
{
    addCors();
    server.send(code, "application/json", body);
}

static void sendOptions()
{
    addCors();
    server.send(204);
}

static bool serveWebAsset(const String &path)
{
    for (size_t i = 0; i < WEB_ASSET_COUNT; ++i) {
        if (path != WEB_ASSETS[i].path) {
            continue;
        }
        server.sendHeader("Cache-Control", "no-cache");
        addCors();
        server.send_P(200, WEB_ASSETS[i].mime, (const char *)WEB_ASSETS[i].data, WEB_ASSETS[i].len);
        return true;
    }
    return false;
}

void EpdNetDisplay::begin(EpdStore &store, EpdBitmapDisplay &renderer, ShowNotify onShown)
{
    store_ = &store;
    renderer_ = &renderer;
    onShown_ = onShown;
    gNet = this;
    gStore = &store;
    gRenderer = &renderer;

#if EPD_WIFI_AP_MODE
    WiFi.mode(WIFI_AP);
    WiFi.softAP(EPD_WIFI_SSID, EPD_WIFI_PASS);
    ESP_LOGI(TAG, "AP %s -> http:
#else
    WiFi.mode(WIFI_STA);
    WiFi.begin(EPD_WIFI_STA_SSID, EPD_WIFI_STA_PASS);
    for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; ++i) {
        delay(250);
    }
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(EPD_WIFI_SSID, EPD_WIFI_PASS);
    }
#endif

    setupRoutes();
    server.begin();
}

String EpdNetDisplay::ipAddress() const
{
    if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
        return WiFi.softAPIP().toString();
    }
    return WiFi.localIP().toString();
}

void EpdNetDisplay::showItemById(const String &id)
{
    if (!store_ || !renderer_) {
        return;
    }
    store_->show(id, [this](const EpdItemMeta &m, const uint8_t *d, size_t n) {
        renderer_->showItem(m, d, n);
        if (onShown_) {
            onShown_();
        }
    });
}

void EpdNetDisplay::setupRoutes()
{
    server.on("/", HTTP_GET, []() {
        if (!serveWebAsset("/")) {
            server.send(404, "text/plain", "web missing");
        }
    });

    server.on("/app.js", HTTP_GET, []() {
        if (!serveWebAsset("/app.js")) {
            server.send(404, "text/plain", "missing");
        }
    });

    server.on("/style.css", HTTP_GET, []() {
        if (!serveWebAsset("/style.css")) {
            server.send(404, "text/plain", "missing");
        }
    });

    server.on("/api/items", HTTP_OPTIONS, sendOptions);
    server.on("/api/data", HTTP_OPTIONS, sendOptions);
    server.on("/api/show", HTTP_OPTIONS, sendOptions);
    server.on("/display", HTTP_OPTIONS, sendOptions);
    server.on("/api/next", HTTP_OPTIONS, sendOptions);
    server.on("/api/prev", HTTP_OPTIONS, sendOptions);
    server.on("/api/goto", HTTP_OPTIONS, sendOptions);

    server.on("/api/items", HTTP_GET, []() {
        String js;
        gStore->listJson(js);
        sendJson(200, js);
    });

    server.on("/api/data", HTTP_GET, []() {
        if (!server.hasArg("id")) {
            sendJson(400, "{\"error\":\"need id\"}");
            return;
        }
        EpdItemMeta m;
        if (!gStore->findById(server.arg("id"), m)) {
            sendJson(404, "{\"error\":\"not found\"}");
            return;
        }
        size_t len = 0;
        if (!gStore->readData(server.arg("id"), bodyBuf, sizeof(bodyBuf), len)) {
            sendJson(500, "{\"error\":\"read failed\"}");
            return;
        }
        addCors();
        if (m.type == EpdItemType::Text) {
            server.send_P(200, "text/plain; charset=utf-8", (const char *)bodyBuf, len);
        } else {
            server.send_P(200, "application/octet-stream", (const char *)bodyBuf, len);
        }
    });

    server.on("/api/items", HTTP_DELETE, []() {
        if (!server.hasArg("id")) {
            sendJson(400, "{\"error\":\"need id\"}");
            return;
        }
        if (gStore->remove(server.arg("id"))) {
            String js;
            gStore->listJson(js);
            sendJson(200, js);
        } else {
            sendJson(404, "{\"error\":\"not found\"}");
        }
    });

    server.on("/api/items", HTTP_POST, []() {
        if (!server.hasArg("name")) {
            sendJson(400, "{\"error\":\"need name\"}");
            return;
        }
        String name = server.arg("name");
        String type = server.hasArg("type") ? server.arg("type") : "bitmap";
        EpdItemType it = (type == "text") ? EpdItemType::Text : EpdItemType::Bitmap;

        WiFiClient client = server.client();
        int contentLen = server.header("Content-Length").toInt();
        if (contentLen <= 0) {
            sendJson(400, "{\"error\":\"empty body\"}");
            return;
        }

        size_t need = (it == EpdItemType::Bitmap) ? EPD_BITMAP_BYTES : (size_t)contentLen;
        if (it == EpdItemType::Bitmap && contentLen < (int)EPD_BITMAP_BYTES) {
            sendJson(400, "{\"error\":\"need 5000 bytes\"}");
            return;
        }
        if (it == EpdItemType::Text && contentLen > (int)EpdStore::MAX_TEXT_BYTES) {
            sendJson(400, "{\"error\":\"text too long\"}");
            return;
        }

        size_t got = 0;
        uint32_t t0 = millis();
        while (got < need && (client.available() || millis() - t0 < 12000)) {
            if (client.available()) {
                got += client.read(bodyBuf + got, need - got);
            } else {
                delay(1);
            }
        }
        if (got < need) {
            sendJson(408, "{\"error\":\"timeout\"}");
            return;
        }

        String id;
        if (gStore->create(name, it, bodyBuf, got, id)) {
            sendJson(201, String("{\"ok\":true,\"id\":\"") + id + "\"}");
        } else {
            sendJson(500, "{\"error\":\"create failed\"}");
        }
    });

    server.on("/api/show", HTTP_POST, []() {
        if (!server.hasArg("id")) {
            sendJson(400, "{\"error\":\"need id\"}");
            return;
        }
        EpdItemMeta m;
        if (!gStore->findById(server.arg("id"), m)) {
            sendJson(404, "{\"error\":\"not found\"}");
            return;
        }
        gNet->showItemById(server.arg("id"));
        sendJson(200, "{\"ok\":true}");
    });

    server.on("/display", HTTP_POST, []() {
        WiFiClient client = server.client();
        size_t got = 0;
        uint32_t t0 = millis();
        while (got < EPD_BITMAP_BYTES && (client.available() || millis() - t0 < 8000)) {
            if (client.available()) {
                got += client.read(bodyBuf + got, EPD_BITMAP_BYTES - got);
            } else {
                delay(1);
            }
        }
        if (got < EPD_BITMAP_BYTES) {
            addCors();
            server.send(400, "text/plain", "need 5000 bytes");
            return;
        }
        gRenderer->showBitmap(bodyBuf, EPD_BITMAP_BYTES);
        if (gNet->onShown_) {
            gNet->onShown_();
        }
        addCors();
        server.send(200, "text/plain", "ok");
    });

    server.on("/api/next", HTTP_POST, []() {
        if (gStore->showNext([](const EpdItemMeta &m, const uint8_t *d, size_t n) {
                gRenderer->showItem(m, d, n);
                if (gNet->onShown_) {
                    gNet->onShown_();
                }
            })) {
            String js;
            gStore->listJson(js);
            sendJson(200, js);
        } else {
            sendJson(400, "{\"error\":\"empty\"}");
        }
    });

    server.on("/api/prev", HTTP_POST, []() {
        if (gStore->showPrev([](const EpdItemMeta &m, const uint8_t *d, size_t n) {
                gRenderer->showItem(m, d, n);
                if (gNet->onShown_) {
                    gNet->onShown_();
                }
            })) {
            String js;
            gStore->listJson(js);
            sendJson(200, js);
        } else {
            sendJson(400, "{\"error\":\"empty\"}");
        }
    });

    server.on("/api/goto", HTTP_POST, []() {
        if (!server.hasArg("idx")) {
            sendJson(400, "{\"error\":\"need idx\"}");
            return;
        }
        int idx = server.arg("idx").toInt();
        if (idx < 0 || !gStore->showByIndex((uint16_t)idx, [](const EpdItemMeta &m, const uint8_t *d, size_t n) {
                gRenderer->showItem(m, d, n);
                if (gNet->onShown_) {
                    gNet->onShown_();
                }
            })) {
            sendJson(400, "{\"error\":\"bad idx\"}");
            return;
        }
        String js;
        gStore->listJson(js);
        sendJson(200, js);
    });
}

void EpdNetDisplay::loop()
{
    server.handleClient();
}
