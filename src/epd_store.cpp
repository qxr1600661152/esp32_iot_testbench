#include "epd_store.h"
#include "demo_bitmaps.h"
#include "epd_config.h"
#include "esp_log.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

static const char *TAG = "epd_store";
static const char *INDEX_PATH = "/index.json";
static const char *DATA_DIR = "/data";

String EpdStore::newId() const
{
    char buf[9];
    snprintf(buf, sizeof(buf), "%08lx", (unsigned long)(esp_random() & 0xFFFFFFFFu));
    return String(buf);
}

int EpdStore::indexOfId(const String &id) const
{
    for (size_t i = 0; i < count_; ++i) {
        if (items_[i].id == id) {
            return (int)i;
        }
    }
    return -1;
}

int EpdStore::indexOf(const String &id) const
{
    return indexOfId(id);
}

void EpdStore::reindexAll()
{
    for (size_t i = 0; i < count_; ++i) {
        items_[i].seq = (uint16_t)i;
    }
    clampCurrent();
}

void EpdStore::clampCurrent()
{
    if (count_ == 0) {
        currentIndex_ = 0;
    } else if (currentIndex_ >= count_) {
        currentIndex_ = (uint16_t)(count_ - 1);
    }
}

void EpdStore::setCurrentIndex(uint16_t idx)
{
    if (count_ == 0) {
        currentIndex_ = 0;
        return;
    }
    if (idx >= count_) {
        idx = (uint16_t)(count_ - 1);
    }
    currentIndex_ = idx;
}

bool EpdStore::findByIndex(uint16_t idx, EpdItemMeta &out) const
{
    if (idx >= count_) {
        return false;
    }
    out = items_[idx];
    return true;
}

bool EpdStore::dataPath(const String &id, EpdItemType type, String &path) const
{
    path = String(DATA_DIR) + "/" + id;
    path += (type == EpdItemType::Text) ? ".txt" : ".epd";
    return true;
}

bool EpdStore::begin()
{
    const char *part = "littlefs";
    if (!LittleFS.begin(false, "/littlefs", 10, part)) {
        ESP_LOGW(TAG, "LittleFS mount failed, formatting %s...", part);
        if (!LittleFS.begin(true, "/littlefs", 10, part)) {
            ESP_LOGE(TAG, "LittleFS format/mount failed on %s", part);
            return false;
        }
    }
    if (!LittleFS.exists(DATA_DIR)) {
        if (!LittleFS.mkdir(DATA_DIR)) {
            ESP_LOGE(TAG, "mkdir %s failed", DATA_DIR);
            return false;
        }
    }
    if (!loadIndex()) {
        count_ = 0;
        currentIndex_ = 0;
        saveIndex();
    }
    reindexAll();
    ESP_LOGI(TAG, "store ready, %u items, current=%u", (unsigned)count_, (unsigned)currentIndex_);
    return true;
}

bool EpdStore::loadIndex()
{
    if (!LittleFS.exists(INDEX_PATH)) {
        return false;
    }
    File f = LittleFS.open(INDEX_PATH, "r");
    if (!f) {
        return false;
    }
    JsonDocument doc;
    if (deserializeJson(doc, f)) {
        f.close();
        return false;
    }
    f.close();

    count_ = 0;
    currentIndex_ = doc["current"] | 0;
    JsonArray arr = doc["items"].as<JsonArray>();
    for (JsonObject o : arr) {
        if (count_ >= MAX_ITEMS) {
            break;
        }
        EpdItemMeta &m = items_[count_++];
        m.id = o["id"].as<const char *>();
        m.name = o["name"].as<const char *>();
        m.type = (o["type"].as<String>() == "text") ? EpdItemType::Text : EpdItemType::Bitmap;
        m.size = o["size"] | 0;
        m.updated = o["updated"] | 0;
        m.seq = o["seq"] | (uint16_t)(count_ - 1);
    }
    return true;
}

bool EpdStore::saveIndex()
{
    JsonDocument doc;
    doc["current"] = currentIndex_;
    JsonArray arr = doc["items"].to<JsonArray>();
    for (size_t i = 0; i < count_; ++i) {
        JsonObject o = arr.add<JsonObject>();
        o["id"] = items_[i].id;
        o["name"] = items_[i].name;
        o["type"] = (items_[i].type == EpdItemType::Text) ? "text" : "bitmap";
        o["size"] = items_[i].size;
        o["updated"] = items_[i].updated;
        o["seq"] = items_[i].seq;
    }
    File f = LittleFS.open(INDEX_PATH, "w");
    if (!f) {
        return false;
    }
    if (serializeJson(doc, f) == 0) {
        f.close();
        return false;
    }
    f.close();
    return true;
}

bool EpdStore::listJson(String &out) const
{
    JsonDocument doc;
    JsonArray arr = doc["items"].to<JsonArray>();
    for (size_t i = 0; i < count_; ++i) {
        JsonObject o = arr.add<JsonObject>();
        o["id"] = items_[i].id;
        o["name"] = items_[i].name;
        o["type"] = (items_[i].type == EpdItemType::Text) ? "text" : "bitmap";
        o["size"] = items_[i].size;
        o["updated"] = items_[i].updated;
        o["seq"] = items_[i].seq;
    }
    doc["count"] = count_;
    doc["current"] = currentIndex_;
    serializeJson(doc, out);
    return true;
}

bool EpdStore::findById(const String &id, EpdItemMeta &out) const
{
    int i = indexOfId(id);
    if (i < 0) {
        return false;
    }
    out = items_[i];
    return true;
}

bool EpdStore::findByName(const String &name, EpdItemMeta &out) const
{
    for (size_t i = 0; i < count_; ++i) {
        if (items_[i].name == name) {
            out = items_[i];
            return true;
        }
    }
    return false;
}

bool EpdStore::create(const String &name, EpdItemType type, const uint8_t *data, size_t len, String &outId)
{
    if (!data || len == 0) {
        return false;
    }
    if (type == EpdItemType::Bitmap && len != EPD_BITMAP_BYTES) {
        ESP_LOGE(TAG, "bitmap size must be %d", EPD_BITMAP_BYTES);
        return false;
    }
    if (type == EpdItemType::Text && len > MAX_TEXT_BYTES) {
        return false;
    }
    if (count_ >= MAX_ITEMS) {
        return false;
    }

    EpdItemMeta existing;
    if (findByName(name, existing)) {
        if (!update(existing.id, data, len)) {
            return false;
        }
        outId = existing.id;
        return true;
    }

    EpdItemMeta m;
    m.id = newId();
    m.name = name;
    m.type = type;
    m.size = len;
    m.updated = (uint32_t)(millis() / 1000);
    m.seq = (uint16_t)count_;

    String path;
    dataPath(m.id, m.type, path);
    File f = LittleFS.open(path, "w");
    if (!f) {
        ESP_LOGE(TAG, "open write failed: %s", path.c_str());
        return false;
    }
    f.write(data, len);
    f.close();

    items_[count_++] = m;
    reindexAll();
    saveIndex();
    outId = m.id;
    ESP_LOGI(TAG, "created #%u %s (%s)", (unsigned)m.seq, m.name.c_str(), m.id.c_str());
    return true;
}

bool EpdStore::update(const String &id, const uint8_t *data, size_t len)
{
    int i = indexOfId(id);
    if (i < 0 || !data || len == 0) {
        return false;
    }
    EpdItemMeta &m = items_[i];
    if (m.type == EpdItemType::Bitmap && len != EPD_BITMAP_BYTES) {
        return false;
    }
    if (m.type == EpdItemType::Text && len > MAX_TEXT_BYTES) {
        return false;
    }

    String path;
    dataPath(m.id, m.type, path);
    File f = LittleFS.open(path, "w");
    if (!f) {
        return false;
    }
    f.write(data, len);
    f.close();

    m.size = len;
    m.updated = (uint32_t)(millis() / 1000);
    saveIndex();
    ESP_LOGI(TAG, "updated %s", id.c_str());
    return true;
}

bool EpdStore::remove(const String &id)
{
    int i = indexOfId(id);
    if (i < 0) {
        return false;
    }
    if ((uint16_t)i < currentIndex_) {
        --currentIndex_;
    } else if ((uint16_t)i == currentIndex_ && count_ > 1) {
        if (currentIndex_ >= count_ - 1) {
            currentIndex_ = (uint16_t)(count_ - 2);
        }
    }

    String path;
    dataPath(items_[i].id, items_[i].type, path);
    LittleFS.remove(path);

    for (size_t j = (size_t)i + 1; j < count_; ++j) {
        items_[j - 1] = items_[j];
    }
    --count_;
    reindexAll();
    saveIndex();
    ESP_LOGI(TAG, "removed %s, count=%u current=%u", id.c_str(), (unsigned)count_, (unsigned)currentIndex_);
    return true;
}

bool EpdStore::readData(const String &id, uint8_t *buf, size_t bufCap, size_t &outLen) const
{
    EpdItemMeta m;
    if (!findById(id, m)) {
        return false;
    }
    String path;
    dataPath(m.id, m.type, path);
    File f = LittleFS.open(path, "r");
    if (!f) {
        return false;
    }
    outLen = f.read(buf, bufCap);
    f.close();
    return outLen > 0;
}

bool EpdStore::show(const String &id, EpdShowHandler handler)
{
    int i = indexOfId(id);
    if (i < 0 || !handler) {
        return false;
    }
    currentIndex_ = (uint16_t)i;
    saveIndex();

    EpdItemMeta m = items_[i];
    static uint8_t buf[EPD_BITMAP_BYTES > MAX_TEXT_BYTES ? EPD_BITMAP_BYTES : MAX_TEXT_BYTES];
    size_t len = 0;
    if (!readData(id, buf, sizeof(buf), len)) {
        return false;
    }
    handler(m, buf, len);
    return true;
}

bool EpdStore::showByIndex(uint16_t idx, EpdShowHandler handler)
{
    if (idx >= count_ || !handler) {
        return false;
    }
    return show(items_[idx].id, handler);
}

bool EpdStore::showNext(EpdShowHandler handler)
{
    if (count_ == 0 || !handler) {
        return false;
    }
    currentIndex_ = (uint16_t)((currentIndex_ + 1) % count_);
    return show(items_[currentIndex_].id, handler);
}

bool EpdStore::showPrev(EpdShowHandler handler)
{
    if (count_ == 0 || !handler) {
        return false;
    }
    currentIndex_ = (currentIndex_ == 0) ? (uint16_t)(count_ - 1) : (uint16_t)(currentIndex_ - 1);
    return show(items_[currentIndex_].id, handler);
}

bool EpdStore::showCurrent(EpdShowHandler handler)
{
    if (count_ == 0 || !handler) {
        return false;
    }
    return show(items_[currentIndex_].id, handler);
}

bool EpdStore::importDemoAssets()
{
    if (count_ > 0) {
        reindexAll();
        return true;
    }
    static const char *names[] = {"logo", "formula_emc2", "formula_euler", "formula_integral", "formula_sum"};
    for (size_t i = 0; i < DEMO_FRAME_COUNT; ++i) {
        const uint8_t *src = (const uint8_t *)pgm_read_ptr(&DEMO_FRAMES[i]);
        uint8_t tmp[EPD_BITMAP_BYTES];
        memcpy_P(tmp, src, EPD_BITMAP_BYTES);
        String id;
        if (!create(names[i], EpdItemType::Bitmap, tmp, EPD_BITMAP_BYTES, id)) {
            return false;
        }
    }
    currentIndex_ = 0;
    reindexAll();
    saveIndex();
    ESP_LOGI(TAG, "imported %u demo items (#0=logo)", (unsigned)DEMO_FRAME_COUNT);
    return true;
}
