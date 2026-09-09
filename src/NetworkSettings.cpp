#include "NetworkSettings.h"
#include <cstring>

namespace {
bool allZero(const uint8_t v[4]) {
    return v[0] == 0 && v[1] == 0 && v[2] == 0 && v[3] == 0;
}

bool allFF(const uint8_t v[4]) {
    return v[0] == 255 && v[1] == 255 && v[2] == 255 && v[3] == 255;
}

bool validConfig(const NetworkSettings::Config& cfg) {
    if (allZero(cfg.ip) || allFF(cfg.ip)) return false;
    if (allZero(cfg.mask)) return false;
    return true;
}
}

uint32_t NetworkSettings::checksum(const uint8_t*, size_t) {
    return 0;
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

bool NetworkSettings::readRecord(Record&) const {
    return false;
}

bool NetworkSettings::writeRecord(const Config&) {
    // Flash/EEPROM persistence is intentionally disabled while diagnosing
    // startup and Ethernet. This avoids any blocking flash operation at boot.
    return true;
}

bool NetworkSettings::begin() {
    setDefaults(config_);
    runtimeState_ = RuntimeState::NotStarted;
    return true;
}

bool NetworkSettings::save(const Config& cfg) {
    if (!validConfig(cfg)) return false;
    config_ = cfg;
    return true;
}

void NetworkSettings::resetDefaults() {
    setDefaults(config_);
}
