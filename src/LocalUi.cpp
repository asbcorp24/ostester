#include "LocalUi.h"
#include <Wire.h>
#include <STM32Ethernet.h>

LocalUi::LocalUi(NetworkSettings& settings)
    : settings_(settings),
      display_(U8G2_R0, U8X8_PIN_NONE) {}

uint8_t LocalUi::scanI2c() {
    Wire.beginTransmission(0x3C);
    if (Wire.endTransmission() == 0) return 0x3C;

    Wire.beginTransmission(0x3D);
    if (Wire.endTransmission() == 0) return 0x3D;

    return 0;
}

bool LocalUi::begin() {
    Wire.setSCL(PB8);
    Wire.setSDA(PB9);
    Wire.begin();
    Wire.setClock(100000);

    oledAddress_ = scanI2c();
    if (oledAddress_ == 0) return false;

    display_.setI2CAddress(static_cast<uint8_t>(oledAddress_ << 1));
    display_.begin();
    display_.setFont(u8g2_font_6x12_tf);
    oledReady_ = true;

    display_.clearBuffer();
    display_.drawStr(0, 14, "OSTester BOOT");
    display_.setCursor(0, 32);
    display_.print("OLED 0x");
    if (oledAddress_ < 16) display_.print('0');
    display_.print(oledAddress_, HEX);
    display_.drawStr(0, 50, "Starting...");
    display_.sendBuffer();

    edit_ = settings_.config();
    lastAutoPageMs_ = millis();
    autoDiagPage_ = 0;
    return true;
}

void LocalUi::loop() {
    if (!oledReady_) return;

    const uint32_t now = millis();

    if (screen_ == Screen::Home && now - lastAutoPageMs_ >= 2000) {
        lastAutoPageMs_ = now;
        autoDiagPage_ = static_cast<uint8_t>((autoDiagPage_ + 1) % 4);
        draw();
        return;
    }

    if ((screen_ == Screen::Home || screen_ == Screen::Diagnostics) && now - lastDrawMs_ >= 500) {
        draw();
    }
}

void LocalUi::handleRotation(int) {}
void LocalUi::handleClick() {}

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

void LocalUi::printIp(U8G2& d, const IPAddress& ip) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    d.print(buf);
}

const char* LocalUi::runtimeStateText() const {
    switch (settings_.runtimeState()) {
        case NetworkSettings::RuntimeState::DhcpOk:       return "DHCP OK";
        case NetworkSettings::RuntimeState::DhcpFallback: return "FALLBACK";
        case NetworkSettings::RuntimeState::Static:       return "STATIC";
        default:                                          return "NOT STARTED";
    }
}

void LocalUi::draw() {
    if (!oledReady_) return;
    lastDrawMs_ = millis();
    display_.clearBuffer();
    display_.setFont(u8g2_font_6x12_tf);

    switch (screen_) {
        case Screen::Home:        drawHome(); break;
        case Screen::Diagnostics: drawDiagnostics(); break;
        case Screen::Menu:        drawMenu(); break;
        case Screen::EditIp:      drawEditIp(); break;
    }

    display_.sendBuffer();
}

void LocalUi::drawHome() {
    const auto& cfg = settings_.config();

    display_.setCursor(0, 10);
    display_.print("OSTester NET ");
    display_.print(autoDiagPage_ + 1);
    display_.print("/4");
    display_.drawHLine(0, 13, 128);

    if (autoDiagPage_ == 0) {
        display_.setCursor(0, 27); display_.print("State: "); display_.print(runtimeStateText());
        display_.setCursor(0, 40); display_.print("Link: ");
        const EthernetLinkStatus ls = Ethernet.linkStatus();
        if (ls == LinkON) display_.print("UP");
        else if (ls == LinkOFF) display_.print("DOWN");
        else display_.print("UNKNOWN");
        display_.setCursor(0, 53); display_.print("IP: "); printIp(display_, Ethernet.localIP());
        display_.setCursor(0, 64); display_.print("OLED:0x"); if (oledAddress_ < 16) display_.print('0'); display_.print(oledAddress_, HEX);
    } else if (autoDiagPage_ == 1) {
        display_.setCursor(0, 27); display_.print("IP  "); printIp(display_, Ethernet.localIP());
        display_.setCursor(0, 40); display_.print("MSK "); printIp(display_, Ethernet.subnetMask());
        display_.setCursor(0, 53); display_.print("GW  "); printIp(display_, Ethernet.gatewayIP());
        display_.setCursor(0, 64); display_.print("DNS "); printIp(display_, Ethernet.dnsServerIP());
    } else if (autoDiagPage_ == 2) {
        display_.setCursor(0, 27); display_.print("Saved: "); display_.print(cfg.dhcp ? "DHCP" : "STATIC");
        display_.setCursor(0, 40); display_.print("IP  "); printIp(display_, cfg.ip);
        display_.setCursor(0, 53); display_.print("MSK "); printIp(display_, cfg.mask);
        display_.setCursor(0, 64); display_.print("GW  "); printIp(display_, cfg.gateway);
    } else {
        display_.setCursor(0, 27); display_.print("DNS "); printIp(display_, cfg.dns);
        display_.setCursor(0, 40); display_.print("I2C 0x"); if (oledAddress_ < 16) display_.print('0'); display_.print(oledAddress_, HEX);
        display_.setCursor(0, 53); display_.print("SCL PB8 SDA PB9");
        display_.setCursor(0, 64); display_.print("ENC DISABLED");
    }
}

void LocalUi::drawDiagnostics() { drawHome(); }

void LocalUi::drawMenu() {
    display_.drawStr(0, 14, "ENCODER DISABLED");
    display_.drawStr(0, 32, "OLED diagnostics");
    display_.drawStr(0, 50, "auto mode");
}

void LocalUi::drawEditIp() {
    display_.drawStr(0, 14, "ENCODER DISABLED");
}
