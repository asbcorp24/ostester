#include "LocalUi.h"
#include <Wire.h>
#include <STM32Ethernet.h>

LocalUi::LocalUi(NetworkSettings& settings)
    : settings_(settings),
      display_(U8G2_R0, U8X8_PIN_NONE),
      encoder_(ENC_A, ENC_B) {}

bool LocalUi::begin() {
    pinMode(ENC_SW, INPUT_PULLUP);

    Wire.setSCL(PB8);
    Wire.setSDA(PB9);
    Wire.begin();

    display_.begin();
    display_.setFont(u8g2_font_6x12_tf);

    edit_ = settings_.config();
    encoder_.write(0);
    lastEncoder_ = 0;
    draw();
    return true;
}

void LocalUi::loop() {
    const long raw = encoder_.read();
    const long detent = raw / 4;
    if (detent != lastEncoder_) {
        const int delta = (detent > lastEncoder_) ? 1 : -1;
        lastEncoder_ = detent;
        handleRotation(delta);
        draw();
    }

    const bool button = digitalRead(ENC_SW);
    const uint32_t now = millis();
    if (lastButton_ && !button && (now - lastButtonMs_) > 180) {
        lastButtonMs_ = now;
        handleClick();
        draw();
    }
    lastButton_ = button;

    if (screen_ == Screen::Home && now - lastDrawMs_ >= 1000) {
        draw();
    }
}

void LocalUi::handleRotation(int delta) {
    if (screen_ == Screen::Menu) {
        menuIndex_ += delta;
        if (menuIndex_ < 0) menuIndex_ = 6;
        if (menuIndex_ > 6) menuIndex_ = 0;
        return;
    }

    if (screen_ == Screen::EditIp) {
        uint8_t* b = fieldBytes(editField_);
        int v = static_cast<int>(b[octet_]) + delta;
        if (v < 0) v = 255;
        if (v > 255) v = 0;
        b[octet_] = static_cast<uint8_t>(v);
    }
}

void LocalUi::handleClick() {
    if (screen_ == Screen::Home) {
        edit_ = settings_.config();
        menuIndex_ = 0;
        screen_ = Screen::Menu;
        return;
    }

    if (screen_ == Screen::EditIp) {
        ++octet_;
        if (octet_ >= 4) {
            octet_ = 0;
            screen_ = Screen::Menu;
        }
        return;
    }

    switch (menuIndex_) {
        case 0:
            edit_.dhcp = !edit_.dhcp;
            break;
        case 1:
            editField_ = Field::Ip;
            octet_ = 0;
            screen_ = Screen::EditIp;
            break;
        case 2:
            editField_ = Field::Mask;
            octet_ = 0;
            screen_ = Screen::EditIp;
            break;
        case 3:
            editField_ = Field::Gateway;
            octet_ = 0;
            screen_ = Screen::EditIp;
            break;
        case 4:
            editField_ = Field::Dns;
            octet_ = 0;
            screen_ = Screen::EditIp;
            break;
        case 5:
            settings_.save(edit_);
            display_.clearBuffer();
            display_.setFont(u8g2_font_6x12_tf);
            display_.drawStr(0, 20, "Settings saved");
            display_.drawStr(0, 38, "Rebooting...");
            display_.sendBuffer();
            delay(250);
            NVIC_SystemReset();
            break;
        case 6:
            edit_ = settings_.config();
            screen_ = Screen::Home;
            break;
    }
}

uint8_t* LocalUi::fieldBytes(Field field) {
    switch (field) {
        case Field::Ip:      return edit_.ip;
        case Field::Mask:    return edit_.mask;
        case Field::Gateway: return edit_.gateway;
        case Field::Dns:     return edit_.dns;
    }
    return edit_.ip;
}

const char* LocalUi::fieldName(Field field) const {
    switch (field) {
        case Field::Ip:      return "IP";
        case Field::Mask:    return "MASK";
        case Field::Gateway: return "GATEWAY";
        case Field::Dns:     return "DNS";
    }
    return "IP";
}

void LocalUi::printIp(U8G2& d, const uint8_t ip[4]) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    d.print(buf);
}

void LocalUi::draw() {
    lastDrawMs_ = millis();
    display_.clearBuffer();
    display_.setFont(u8g2_font_6x12_tf);

    switch (screen_) {
        case Screen::Home:   drawHome(); break;
        case Screen::Menu:   drawMenu(); break;
        case Screen::EditIp: drawEditIp(); break;
    }

    display_.sendBuffer();
}

void LocalUi::drawHome() {
    display_.drawStr(0, 10, "OSTester");
    display_.drawHLine(0, 13, 128);

    display_.setCursor(0, 27);
    display_.print("IP: ");
    IPAddress current = Ethernet.localIP();
    uint8_t a[4] = {current[0], current[1], current[2], current[3]};
    printIp(display_, a);

    display_.setCursor(0, 41);
    display_.print("Mode: ");
    display_.print(settings_.config().dhcp ? "DHCP" : "STATIC");

    display_.setCursor(0, 55);
    display_.print("Link: ");
    display_.print(Ethernet.linkStatus() == LinkON ? "UP" : "DOWN");

    display_.drawStr(92, 63, "MENU");
}

void LocalUi::drawMenu() {
    static const char* items[] = {
        "DHCP", "IP", "MASK", "GATEWAY", "DNS", "SAVE+REBOOT", "BACK"
    };

    display_.drawStr(0, 10, "NETWORK SETTINGS");
    display_.drawHLine(0, 13, 128);

    const int first = (menuIndex_ <= 2) ? 0 : menuIndex_ - 2;
    for (int row = 0; row < 4; ++row) {
        const int idx = first + row;
        if (idx > 6) break;
        const int y = 27 + row * 12;
        display_.setCursor(0, y);
        display_.print(idx == menuIndex_ ? ">" : " ");
        display_.print(items[idx]);
        if (idx == 0) {
            display_.print(": ");
            display_.print(edit_.dhcp ? "ON" : "OFF");
        }
    }
}

void LocalUi::drawEditIp() {
    display_.setCursor(0, 10);
    display_.print("EDIT ");
    display_.print(fieldName(editField_));
    display_.drawHLine(0, 13, 128);

    uint8_t* b = fieldBytes(editField_);
    display_.setCursor(0, 32);
    printIp(display_, b);

    display_.setCursor(0, 49);
    display_.print("Octet ");
    display_.print(octet_ + 1);
    display_.print(" = ");
    display_.print(b[octet_]);

    display_.drawStr(0, 63, "Rotate / press=next");
}
