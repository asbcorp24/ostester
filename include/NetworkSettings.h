#pragma once

#include <Arduino.h>

class NetworkSettings {
public:
    struct Config {
        bool dhcp;
        uint8_t ip[4];
        uint8_t mask[4];
        uint8_t gateway[4];
        uint8_t dns[4];
    };

    bool begin();
    const Config& config() const { return config_; }
    bool save(const Config& cfg);
    void resetDefaults();

    static IPAddress toIp(const uint8_t v[4]) { return IPAddress(v[0], v[1], v[2], v[3]); }

private:
    static constexpr uint32_t MAGIC = 0x4F534E45u; // 'OSNE'
    static constexpr uint16_t VERSION = 1;
    static constexpr int EEPROM_ADDR = 0;

    struct Record {
        uint32_t magic;
        uint16_t version;
        uint8_t dhcp;
        uint8_t reserved;
        uint8_t ip[4];
        uint8_t mask[4];
        uint8_t gateway[4];
        uint8_t dns[4];
        uint32_t checksum;
    };

    Config config_{};

    static uint32_t checksum(const uint8_t* data, size_t len);
    static void setDefaults(Config& cfg);
    bool readRecord(Record& rec) const;
    bool writeRecord(const Config& cfg);
};
