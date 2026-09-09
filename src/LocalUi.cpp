#include "LocalUi.h"
#include <Wire.h>
#include <STM32Ethernet.h>

extern "C" void HAL_TIM_Encoder_MspInit(TIM_HandleTypeDef* htim) {
    if (htim == nullptr || htim->Instance != TIM4) return;

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_TIM4_CLK_ENABLE();

    GPIO_InitTypeDef gpio{};
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = GPIO_AF2_TIM4;
    HAL_GPIO_Init(GPIOB, &gpio);
}

LocalUi::LocalUi(NetworkSettings& settings)
    : settings_(settings),
      display_(U8G2_R0, U8X8_PIN_NONE) {}

uint8_t LocalUi::scanI2c() {
    Serial.println("I2C scan on PB8(SCL) / PB9(SDA)...");
    uint8_t first = 0;
    uint8_t found = 0;

    for (uint8_t addr = 1; addr < 127; ++addr) {
        Wire.beginTransmission(addr);
        const uint8_t err = Wire.endTransmission();
        if (err == 0) {
            ++found;
            if (first == 0) first = addr;
            Serial.print("  I2C device found at 0x");
            if (addr < 16) Serial.print('0');
            Serial.println(addr, HEX);
        }
    }

    if (found == 0) {
        Serial.println("  No I2C devices found");
        return 0;
    }

    for (uint8_t preferred : {static_cast<uint8_t>(0x3C), static_cast<uint8_t>(0x3D)}) {
        Wire.beginTransmission(preferred);
        if (Wire.endTransmission() == 0) return preferred;
    }

    return first;
}

bool LocalUi::begin() {
    Wire.setSCL(PB8);
    Wire.setSDA(PB9);
    Wire.begin();
    Wire.setClock(100000);
    delay(20);

    oledAddress_ = scanI2c();
    if (oledAddress_ == 0) {
        Serial.println("OLED: not detected on I2C");
        return false;
    }

    display_.setI2CAddress(static_cast<uint8_t>(oledAddress_ << 1));
    display_.begin();
    display_.setFont(u8g2_font_6x12_tf);
    oledReady_ = true;

    display_.clearBuffer();
    display_.drawStr(0, 14, "OSTester OLED OK");
    display_.setCursor(0, 32);
    display_.print("I2C: 0x");
    if (oledAddress_ < 16) display_.print('0');
    display_.print(oledAddress_, HEX);
    display_.drawStr(0, 50, "Starting system...");
    display_.sendBuffer();

    // Encoder is optional. The OLED diagnostics work without it.
    encoder_ = new STM32encoder(TIM4, 8, 3);
    if (encoder_ != nullptr && encoder_->isStarted()) {
        encoder_->setButton(ENC_SW, BTN_POLL);
        encoder_->pos(0);
        lastEncoder_ = 0;
    } else {
        Serial.println("Encoder not available - OLED auto diagnostics enabled");
        if (encoder_ != nullptr) {
            delete encoder_;
            encoder_ = nullptr;
        }
    }

    edit_ = settings_.config();
    lastAutoPageMs_ = millis();
    autoDiagPage_ = 0;
    draw();
    return true;
}

void LocalUi::loop() {
    if (!oledReady_) return;

    if (encoder_ && encoder_->isUpdated()) {
        const int32_t current = encoder_->pos();
        if (current != lastEncoder_) {
            const int delta = (current > lastEncoder_) ? 1 : -1;
            lastEncoder_ = current;
            handleRotation(delta);
            draw();
        }
    }

    if (encoder_) {
        const enc_events_t evt = encoder_->button();
        if (evt == BTN_EVT_CLICK) {
            handleClick();
            draw();
        }
    }

    const uint32_t now = millis();

    // No encoder required: rotate the main diagnostics automatically.
    if (screen_ == Screen::Home && now - lastAutoPageMs_ >= 2000) {
        lastAutoPageMs_ = now;
        autoDiagPage_ = static_cast<uint8_t>((autoDiagPage_ + 1) % 4);
        draw();
        return;
    }

    if ((screen_ == Screen::Home || screen_ == Screen::Diagnostics) && now - lastDrawMs_ >= 1000) {
        draw();
    }
}

void LocalUi::handleRotation(int delta) {
    if (screen_ == Screen::Home) {
        if (delta > 0) { screen_ = Screen::Diagnostics; diagPage_ = 0; }
        return;
    }

    if (screen_ == Screen::Diagnostics) {
        diagPage_ += delta;
        if (diagPage_ < 0) diagPage_ = 3;
        if (diagPage_ > 3) diagPage_ = 0;
        return;
    }

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

    if (screen_ == Screen::Diagnostics) {
        screen_ = Screen::Home;
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

void LocalUi::printIp(U8G2& d, const IPAddress& ip) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    d.print(buf);
}

const char* LocalUi::runtimeStateText() const {
    switch (settings_.runtimeState()) {
        case NetworkSettings::RuntimeState::DhcpOk:       return "DHCP OK";
        case NetworkSettings::RuntimeState::DhcpFallback: return "DHCP->FALLBACK";
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
        display_.setCursor(0, 40); display_.print("Link: "); display_.print(Ethernet.linkStatus() == LinkON ? "UP" : "DOWN");
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
        display_.setCursor(0, 27); display_.print("Saved DNS "); printIp(display_, cfg.dns);
        display_.setCursor(0, 40); display_.print("I2C 0x"); if (oledAddress_ < 16) display_.print('0'); display_.print(oledAddress_, HEX);
        display_.setCursor(0, 53); display_.print("SCL PB8 SDA PB9");
        display_.setCursor(0, 64); display_.print("ENC optional");
    }
}

void LocalUi::drawDiagnostics() {
    const auto& cfg = settings_.config();

    display_.setCursor(0, 10);
    display_.print("DIAG ");
    display_.print(diagPage_ + 1);
    display_.print("/4");
    display_.drawHLine(0, 13, 128);

    if (diagPage_ == 0) {
        display_.setCursor(0, 27); display_.print("State: "); display_.print(runtimeStateText());
        display_.setCursor(0, 40); display_.print("Link: "); display_.print(Ethernet.linkStatus() == LinkON ? "UP" : "DOWN");
        display_.setCursor(0, 53); display_.print("OLED: 0x"); if (oledAddress_ < 16) display_.print('0'); display_.print(oledAddress_, HEX);
        display_.setCursor(0, 64); display_.print("ENC: "); display_.print(encoder_ && encoder_->isStarted() ? "OK" : "N/A");
    } else if (diagPage_ == 1) {
        display_.setCursor(0, 27); display_.print("IP "); printIp(display_, Ethernet.localIP());
        display_.setCursor(0, 40); display_.print("MSK "); printIp(display_, Ethernet.subnetMask());
        display_.setCursor(0, 53); display_.print("GW "); printIp(display_, Ethernet.gatewayIP());
        display_.setCursor(0, 64); display_.print("DNS "); printIp(display_, Ethernet.dnsServerIP());
    } else if (diagPage_ == 2) {
        display_.setCursor(0, 27); display_.print("Saved:"); display_.print(cfg.dhcp ? " DHCP" : " STATIC");
        display_.setCursor(0, 40); display_.print("IP "); printIp(display_, cfg.ip);
        display_.setCursor(0, 53); display_.print("MSK "); printIp(display_, cfg.mask);
        display_.setCursor(0, 64); display_.print("GW "); printIp(display_, cfg.gateway);
    } else {
        display_.setCursor(0, 27); display_.print("DNS "); printIp(display_, cfg.dns);
        display_.setCursor(0, 40); display_.print("SCL PB8 SDA PB9");
        display_.setCursor(0, 53); display_.print("ENC optional");
        display_.setCursor(0, 64); display_.print("OLED auto diag");
    }
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
