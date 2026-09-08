#pragma once

#include <Arduino.h>

class ScriptStore {
public:
    static constexpr uint8_t MAX_SCRIPTS = 6;
    static constexpr size_t MAX_NAME = 31;
    static constexpr size_t MAX_SCRIPT = 1900;

    struct EntryInfo {
        bool used;
        char name[MAX_NAME + 1];
        uint16_t length;
        uint32_t checksum;
    };

    bool begin();
    bool save(const String& name, const String& code, String* error = nullptr);
    bool load(const String& name, String& code, String* error = nullptr) const;
    bool remove(const String& name, String* error = nullptr);
    uint8_t list(EntryInfo* out, uint8_t maxEntries) const;

private:
    static constexpr uint32_t STORE_MAGIC = 0x4F535453u; // 'OSTS'
    static constexpr uint16_t STORE_VERSION = 2;
    static constexpr uint32_t SLOT_MAGIC = 0x4C554131u;  // 'LUA1'

    struct Header {
        uint32_t magic;
        uint16_t version;
        uint16_t slotCount;
        uint32_t checksum;
    };

    struct Slot {
        uint32_t magic;
        uint16_t length;
        uint16_t reserved;
        uint32_t checksum;
        char name[MAX_NAME + 1];
        char code[MAX_SCRIPT + 1];
    };

    // 0..255 are reserved for NetworkSettings / local UI configuration.
    static constexpr int HEADER_ADDR = 256;
    static constexpr int SLOT_BASE = HEADER_ADDR + sizeof(Header);

    bool initialized_ = false;

    static int slotAddress(uint8_t index) { return SLOT_BASE + (int)index * (int)sizeof(Slot); }
    static uint32_t checksum(const uint8_t* data, size_t len);
    static bool validName(const String& name);
    static bool namesEqual(const char* stored, const String& name);
    static void setError(String* error, const char* text);

    bool readHeader(Header& h) const;
    bool writeHeader();
    bool readSlot(uint8_t index, Slot& slot) const;
    bool writeSlot(uint8_t index, const Slot& slot);
    bool eraseSlot(uint8_t index);
    int findSlot(const String& name) const;
    int findFreeSlot() const;
};
