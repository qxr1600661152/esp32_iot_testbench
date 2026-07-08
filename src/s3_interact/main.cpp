
#include <Arduino.h>
#include <esp_flash.h>
#include <esp_mac.h>

static const char *BUILD_TAG = "interact-v1-20260625";
static bool gpio21High = false;

static void printBanner()
{
    Serial.println();
    Serial.println("==============================================");
    Serial.println("  ESP32-S3 INTERACT  " + String(BUILD_TAG));
    Serial.println("   = ");
    Serial.println("  : PING | INFO | UPTIME | GPIO21 | HELP");
    Serial.println("==============================================");
}

static void printHelp()
{
    Serial.println("PING          -> PONG + tag");
    Serial.println("INFO          -> chip / flash / MAC");
    Serial.println("UPTIME        -> millis()");
    Serial.println("GPIO21        ->  GPIO21 (READY ，)");
    Serial.println("GPIO21=0|1    ->  GPIO21 ");
    Serial.println("HELP          -> ");
}

static void printInfo()
{
    uint32_t flashSize = 0;
    esp_flash_get_size(NULL, &flashSize);
    Serial.printf("tag=%s\n", BUILD_TAG);
    Serial.printf("chip=%s rev=%d cores=%d\n", ESP.getChipModel(), ESP.getChipRevision(),
                  ESP.getChipCores());
    Serial.printf("cpu_mhz=%u flash_mb=%u psram=%u\n", ESP.getCpuFreqMHz(),
                  (unsigned)(flashSize / (1024 * 1024)), ESP.getPsramSize() / (1024 * 1024));
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    Serial.printf("mac=%02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3],
                  mac[4], mac[5]);
    Serial.printf("free_heap=%u\n", ESP.getFreeHeap());
}

static void handleLine(String line)
{
    line.trim();
    line.toUpperCase();
    if (line.length() == 0) {
        return;
    }
    if (line == "PING") {
        Serial.printf("PONG %s\n", BUILD_TAG);
    } else if (line == "INFO") {
        printInfo();
    } else if (line == "UPTIME") {
        Serial.printf("uptime_ms=%lu\n", millis());
    } else if (line == "GPIO21") {
        gpio21High = !gpio21High;
        digitalWrite(21, gpio21High ? HIGH : LOW);
        Serial.printf("GPIO21=%d\n", gpio21High ? 1 : 0);
    } else if (line.startsWith("GPIO21=")) {
        gpio21High = line.endsWith("1");
        digitalWrite(21, gpio21High ? HIGH : LOW);
        Serial.printf("GPIO21=%d\n", gpio21High ? 1 : 0);
    } else if (line == "HELP") {
        printHelp();
    } else {
        Serial.printf("UNKNOWN cmd=%s (type HELP)\n", line.c_str());
    }
}

void setup()
{
    pinMode(21, OUTPUT);
    digitalWrite(21, LOW);

    Serial.begin(115200);
    delay(800);
    printBanner();
    printInfo();
    Serial.println("ready — type PING and press Enter");
}

void loop()
{
    static uint32_t lastBeat = 0;
    if (millis() - lastBeat >= 2000) {
        lastBeat = millis();
        Serial.printf("[heartbeat] ms=%lu tag=%s\n", millis(), BUILD_TAG);
    }

    static String line;
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            handleLine(line);
            line = "";
        } else if (line.length() < 64) {
            line += c;
        }
    }
    delay(5);
}
