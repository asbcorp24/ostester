#pragma once

#include <Arduino.h>
#include <U8g2lib.h>
#include <Encoder.h>
#include "NetworkSettings.h"

class LocalUi {
public:
    explicit LocalUi(NetworkSettings& settings);
    bool begin();
    void loop();

private:
    enum class Screen : uint8_t { Home, Menu, EditIp };
    enum class Field : uint8_t { Ip, Mask, Gateway, Dns };

    NetworkSettings& settings_;
    NetworkSettings::Config edit_{};
    U8G2_SSD1306_128X64_NONAME_F_HW_I2C display_;
    Encoder encoder_;

    Screen screen_ = Screen::Home;
    Field editField_ = Field::Ip;
    int menuIndex_ = 0;
    int octet_ = 0;
    long lastEncoder_ = 0;
    bool lastButton_ = true;
    uint32_t lastButtonMs_ = 0;
    uint32_t lastDrawMs_ = 0;

    static constexpr uint32_t ENC_A = PG6;
    static constexpr uint32_t ENC_B = PG7;
    static constexpr uint32_t ENC_SW = PG8;

    void draw();
    void drawHome();
    void drawMenu();
    void drawEditIp();
    void handleRotation(int delta);
    void handleClick();
    uint8_t* fieldBytes(Field field);
    const char* fieldName(Field field) const;
    static void printIp(U8G2& d, const uint8_t ip[4]);
};
