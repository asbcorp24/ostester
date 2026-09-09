#pragma once

#include <Arduino.h>
#include <U8g2lib.h>
#include <STM32encoder.h>
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
    STM32encoder* encoder_ = nullptr;

    Screen screen_ = Screen::Home;
    Field editField_ = Field::Ip;
    int menuIndex_ = 0;
    int octet_ = 0;
    int32_t lastEncoder_ = 0;
    uint32_t lastDrawMs_ = 0;
    uint8_t oledAddress_ = 0;
    bool oledReady_ = false;

    static constexpr uint32_t ENC_A = PB6;   // TIM4_CH1
    static constexpr uint32_t ENC_B = PB7;   // TIM4_CH2
    static constexpr uint32_t ENC_SW = PG8;

    uint8_t scanI2c();
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
