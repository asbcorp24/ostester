#pragma once

#include <Arduino.h>
#include <U8g2lib.h>
#include "NetworkSettings.h"

class LocalUi {
public:
    explicit LocalUi(NetworkSettings& settings);
    bool begin();
    void loop();

private:
    enum class Screen : uint8_t { Home, Diagnostics, Menu, EditIp };
    enum class Field : uint8_t { Ip, Mask, Gateway, Dns };

    NetworkSettings& settings_;
    NetworkSettings::Config edit_{};
    U8G2_SSD1306_128X64_NONAME_F_HW_I2C display_;

    Screen screen_ = Screen::Home;
    Field editField_ = Field::Ip;
    int menuIndex_ = 0;
    int octet_ = 0;
    int diagPage_ = 0;
    uint32_t lastDrawMs_ = 0;
    uint32_t lastAutoPageMs_ = 0;
    uint8_t autoDiagPage_ = 0;
    uint8_t oledAddress_ = 0;
    bool oledReady_ = false;

    uint8_t scanI2c();
    void draw();
    void drawHome();
    void drawDiagnostics();
    void drawMenu();
    void drawEditIp();
    void handleRotation(int delta);
    void handleClick();
    uint8_t* fieldBytes(Field field);
    const char* fieldName(Field field) const;
    static void printIp(U8G2& d, const uint8_t ip[4]);
    static void printIp(U8G2& d, const IPAddress& ip);
    const char* runtimeStateText() const;
};
