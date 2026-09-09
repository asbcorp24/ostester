#include "WebServerApp.h"
#include "generated_web.h"

WebServerApp::WebServerApp(ScriptEngine& scripts, NetworkSettings& netSettings)
    : scripts_(scripts), netSettings_(netSettings) {}

bool WebServerApp::begin() {
    store_.begin();

    byte mac[] = {0x02, 0xF7, 0x67, 0x01, 0x00, 0x01};
    const auto& cfg = netSettings_.config();

    Serial.print("ETH: config mode=");
    Serial.println(cfg.dhcp ? "DHCP" : "STATIC");

    // IMPORTANT:
    // Ethernet.begin(mac) performs blocking DHCP and on this board/configuration
    // can leave the boot sequence stuck before the OLED/UI task starts.
    // Therefore boot networking is always brought up immediately with a valid
    // static configuration. If DHCP is enabled, use the known-good fallback
    // address 192.168.1.77. This guarantees that the web UI is reachable.
    if (cfg.dhcp) {
        Serial.println("ETH: DHCP enabled, booting with safe fallback 192.168.1.77");

        IPAddress fallbackIp(192, 168, 1, 77);
        IPAddress fallbackDns(192, 168, 1, 1);
        IPAddress fallbackGateway(192, 168, 1, 1);
        IPAddress fallbackMask(255, 255, 255, 0);

        netSettings_.setRuntimeState(NetworkSettings::RuntimeState::DhcpFallback);
        Ethernet.begin(mac,
                       fallbackIp,
                       fallbackDns,
                       fallbackGateway,
                       fallbackMask);
    } else {
        Serial.println("ETH: applying saved STATIC configuration");
        netSettings_.setRuntimeState(NetworkSettings::RuntimeState::Static);
        Ethernet.begin(mac,
                       NetworkSettings::toIp(cfg.ip),
                       NetworkSettings::toIp(cfg.dns),
                       NetworkSettings::toIp(cfg.gateway),
                       NetworkSettings::toIp(cfg.mask));
    }

    delay(250);

    Serial.print("ETH: localIP=");
    Serial.println(Ethernet.localIP());
    Serial.print("ETH: mask=");
    Serial.println(Ethernet.subnetMask());
    Serial.print("ETH: gateway=");
    Serial.println(Ethernet.gatewayIP());
    Serial.print("ETH: dns=");
    Serial.println(Ethernet.dnsServerIP());

    server_.begin();
    return true;
}

void WebServerApp::loop() {
    EthernetClient client = server_.available();
    if (!client) return;
    handleClient(client);
    client.stop();
}

String WebServerApp::readBody(EthernetClient& client, size_t contentLength) {
    if (contentLength > MAX_BODY) contentLength = MAX_BODY;
    String body;
    body.reserve(contentLength + 1);

    const uint32_t started = millis();
    while (body.length() < contentLength && millis() - started < 3000) {
        while (client.available() && body.length() < contentLength) {
            body += (char)client.read();
        }
        taskYIELD();
    }
    return body;
}
