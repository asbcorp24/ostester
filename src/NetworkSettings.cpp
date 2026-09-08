#include "NetworkSettings.h"
#include <EEPROM.h>
#include <cstring>
#include <cstddef>

namespace {
constexpr uint32_t FNV_OFFSET = 2166136261u;
constexpr uint32_t FNV_PRIME  = 16777619u;
}

uint32_t NetworkSettings::checksum(const uint8_t* data, size_t len) {
    uint32_t h = FNV_OFFSET;
    for (size_t i = 0; i < len; ++i) {
        h ^= data[i];
        h *= FNV_PRIME;
    }
    return h;
}

void NetworkSettings::setDefaults(Config& cfg) {
    cfg.dhcp = true;
    const uint8_t ip[4] = {192,168,1,77};
    const uint8_t mask[4] = {255,255,255,0};
    const uint8_t gateway[4] = {192,168,1,1};
    const uint8_t dns[4] = {192,168,1,1};
    memcpy(cfg.ip, ip, 4);
    memcpy(cfg.mask, mask, 4);
    memcpy(cfg.gateway, gateway, 4);
    memcpy(cfg.dns, dns, 4);
}

bool NetworkSettings::readRecord(Record& rec) const {
    EEPROM.get(EEPROM_ADDR, rec);
    if (rec.magic != MAGIC || rec.version != VERSION) return false;
    const uint32_t expected = checksum(reinterpret_cast<const uint8_t*>(&rec), offsetof(Record, checksum));
    return rec.checksum == expected;
}

bool NetworkSettings::writeRecord(const Config& cfg) {
    Record rec{};
    rec.magic = MAGIC;
    rec.version = VERSION;
    rec.dhcp = cfg.dhcp ? 1 : 0;
    memcpy(rec.ip, cfg.ip, 4);
    memcpy(rec.mask, cfg.mask, 4);
    memcpy(rec.gateway, cfg.gateway, 4);
    memcpy(rec.dns, cfg.dns, 4);
    rec.checksum = checksum(reinterpret_cast<const uint8_t*>(&rec), offsetof(Record, checksum));
    EEPROM.put(EEPROM_ADDR, rec);
    return true;
}

bool NetworkSettings::begin() {
    Record rec{};
    if (!readRecord(rec)) {
        setDefaults(config_);
        return writeRecord(config_);
    }
    config_.dhcp = rec.dhcp != 0;
    memcpy(config_.ip, rec.ip, 4);
    memcpy(config_.mask, rec.mask, 4);
    memcpy(config_.gateway, rec.gateway, 4);
    memcpy(config_.dns, rec.dns, 4);
    return true;
}

bool NetworkSettings::save(const Config& cfg) {
    config_ = cfg;
    return writeRecord(config_);
}

void NetworkSettings::resetDefaults() {
    setDefaults(config_);
    writeRecord(config_);
}
