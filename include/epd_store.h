#pragma once

#include <Arduino.h>
#include <functional>

enum class EpdItemType : uint8_t {
    Bitmap = 0,
    Text = 1,
};

struct EpdItemMeta {
    String id;
    String name;
    EpdItemType type = EpdItemType::Bitmap;
    size_t size = 0;
    uint32_t updated = 0;

    uint16_t seq = 0;
};

using EpdShowHandler = std::function<void(const EpdItemMeta &meta, const uint8_t *data, size_t len)>;

class EpdStore {
public:
    static const size_t MAX_ITEMS = 48;
    static const size_t MAX_TEXT_BYTES = 512;

    bool begin();
    size_t count() const { return count_; }

    bool listJson(String &out) const;
    bool findById(const String &id, EpdItemMeta &out) const;
    bool findByName(const String &name, EpdItemMeta &out) const;

    bool create(const String &name, EpdItemType type, const uint8_t *data, size_t len, String &outId);
    bool update(const String &id, const uint8_t *data, size_t len);
    bool remove(const String &id);

    bool readData(const String &id, uint8_t *buf, size_t bufCap, size_t &outLen) const;
    bool show(const String &id, EpdShowHandler handler);
    bool showByIndex(uint16_t idx, EpdShowHandler handler);
    bool showNext(EpdShowHandler handler);
    bool showPrev(EpdShowHandler handler);
    bool showCurrent(EpdShowHandler handler);

    uint16_t currentIndex() const { return currentIndex_; }
    void setCurrentIndex(uint16_t idx);
    bool findByIndex(uint16_t idx, EpdItemMeta &out) const;

    bool importDemoAssets();

private:
    EpdItemMeta items_[MAX_ITEMS];
    size_t count_ = 0;
    uint16_t currentIndex_ = 0;

    bool loadIndex();
    bool saveIndex();
    void reindexAll();
    void clampCurrent();
    int indexOfId(const String &id) const;
    bool dataPath(const String &id, EpdItemType type, String &path) const;
    String newId() const;
    int indexOf(const String &id) const;
};
