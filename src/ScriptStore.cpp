#include "ScriptStore.h"
#include <EEPROM.h>
#include <cstddef>
#include <cstring>

namespace {
constexpr uint32_t FNV_OFFSET = 2166136261u;
constexpr uint32_t FNV_PRIME  = 16777619u;
}

uint32_t ScriptStore::checksum(const uint8_t* data, size_t len) {
    uint32_t h = FNV_OFFSET;
    for (size_t i = 0; i < len; ++i) {
        h ^= data[i];
        h *= FNV_PRIME;
    }
    return h;
}

bool ScriptStore::validName(const String& name) {
    if (name.length() == 0 || name.length() > MAX_NAME) return false;
    for (size_t i = 0; i < name.length(); ++i) {
        const char c = name[i];
        const bool ok = (c >= 'a' && c <= 'z') ||
                        (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') ||
                        c == '_' || c == '-' || c == '.';
        if (!ok) return false;
    }
    return true;
}

bool ScriptStore::namesEqual(const char* stored, const String& name) {
    return strncmp(stored, name.c_str(), MAX_NAME + 1) == 0;
}

void ScriptStore::setError(String* error, const char* text) {
    if (error) *error = text;
}

bool ScriptStore::readHeader(Header& h) const {
    EEPROM.get(HEADER_ADDR, h);
    if (h.magic != STORE_MAGIC || h.version != STORE_VERSION || h.slotCount != MAX_SCRIPTS) return false;
    const uint32_t expected = checksum(reinterpret_cast<const uint8_t*>(&h), offsetof(Header, checksum));
    return h.checksum == expected;
}

bool ScriptStore::writeHeader() {
    Header h{};
    h.magic = STORE_MAGIC;
    h.version = STORE_VERSION;
    h.slotCount = MAX_SCRIPTS;
    h.checksum = checksum(reinterpret_cast<const uint8_t*>(&h), offsetof(Header, checksum));
    EEPROM.put(HEADER_ADDR, h);
    return true;
}

bool ScriptStore::readSlot(uint8_t index, Slot& slot) const {
    if (index >= MAX_SCRIPTS) return false;
    EEPROM.get(slotAddress(index), slot);
    if (slot.magic != SLOT_MAGIC) return false;
    if (slot.length > MAX_SCRIPT) return false;
    if (slot.name[MAX_NAME] != '\0') return false;
    const uint32_t actual = checksum(reinterpret_cast<const uint8_t*>(slot.code), slot.length);
    return actual == slot.checksum;
}

bool ScriptStore::writeSlot(uint8_t index, const Slot& slot) {
    if (index >= MAX_SCRIPTS) return false;
    EEPROM.put(slotAddress(index), slot);
    return true;
}

bool ScriptStore::eraseSlot(uint8_t index) {
    if (index >= MAX_SCRIPTS) return false;
    Slot empty{};
    EEPROM.put(slotAddress(index), empty);
    return true;
}

bool ScriptStore::begin() {
    Header h{};
    if (!readHeader(h)) {
        writeHeader();
        for (uint8_t i = 0; i < MAX_SCRIPTS; ++i) eraseSlot(i);
    }
    initialized_ = true;
    return true;
}

int ScriptStore::findSlot(const String& name) const {
    Slot slot{};
    for (uint8_t i = 0; i < MAX_SCRIPTS; ++i) {
        if (readSlot(i, slot) && namesEqual(slot.name, name)) return i;
    }
    return -1;
}

int ScriptStore::findFreeSlot() const {
    Slot slot{};
    for (uint8_t i = 0; i < MAX_SCRIPTS; ++i) {
        EEPROM.get(slotAddress(i), slot);
        if (slot.magic != SLOT_MAGIC) return i;
    }
    return -1;
}

bool ScriptStore::save(const String& name, const String& code, String* error) {
    if (!initialized_) { setError(error, "store not initialized"); return false; }
    if (!validName(name)) { setError(error, "invalid name"); return false; }
    if (code.length() == 0) { setError(error, "empty script"); return false; }
    if (code.length() > MAX_SCRIPT) { setError(error, "script too large"); return false; }

    int slotIndex = findSlot(name);
    if (slotIndex < 0) slotIndex = findFreeSlot();
    if (slotIndex < 0) { setError(error, "script store full"); return false; }

    Slot slot{};
    slot.magic = SLOT_MAGIC;
    slot.length = static_cast<uint16_t>(code.length());
    strncpy(slot.name, name.c_str(), MAX_NAME);
    slot.name[MAX_NAME] = '\0';
    memcpy(slot.code, code.c_str(), slot.length);
    slot.code[slot.length] = '\0';
    slot.checksum = checksum(reinterpret_cast<const uint8_t*>(slot.code), slot.length);

    if (!writeSlot(static_cast<uint8_t>(slotIndex), slot)) {
        setError(error, "flash write failed");
        return false;
    }
    return true;
}

bool ScriptStore::load(const String& name, String& code, String* error) const {
    if (!initialized_) { setError(error, "store not initialized"); return false; }
    if (!validName(name)) { setError(error, "invalid name"); return false; }

    const int slotIndex = findSlot(name);
    if (slotIndex < 0) { setError(error, "script not found"); return false; }

    Slot slot{};
    if (!readSlot(static_cast<uint8_t>(slotIndex), slot)) {
        setError(error, "corrupt script");
        return false;
    }

    code = "";
    code.reserve(slot.length + 1);
    for (uint16_t i = 0; i < slot.length; ++i) code += slot.code[i];
    return true;
}

bool ScriptStore::remove(const String& name, String* error) {
    if (!initialized_) { setError(error, "store not initialized"); return false; }
    if (!validName(name)) { setError(error, "invalid name"); return false; }

    const int slotIndex = findSlot(name);
    if (slotIndex < 0) { setError(error, "script not found"); return false; }
    if (!eraseSlot(static_cast<uint8_t>(slotIndex))) {
        setError(error, "flash erase failed");
        return false;
    }
    return true;
}

uint8_t ScriptStore::list(EntryInfo* out, uint8_t maxEntries) const {
    if (!initialized_ || !out || maxEntries == 0) return 0;
    uint8_t count = 0;
    Slot slot{};
    for (uint8_t i = 0; i < MAX_SCRIPTS && count < maxEntries; ++i) {
        if (!readSlot(i, slot)) continue;
        out[count].used = true;
        strncpy(out[count].name, slot.name, MAX_NAME);
        out[count].name[MAX_NAME] = '\0';
        out[count].length = slot.length;
        out[count].checksum = slot.checksum;
        ++count;
    }
    return count;
}
