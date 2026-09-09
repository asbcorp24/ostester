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

    if (cfg.dhcp) {
        const int dhcpResult = Ethernet.begin(mac);
        if (dhcpResult == 0) {
            Serial.println("ETH: DHCP failed -> fallback 192.168.1.77");
            IPAddress fallbackIp(192, 168, 1, 77);
            IPAddress fallbackDns(192, 168, 1, 1);
            IPAddress fallbackGateway(192, 168, 1, 1);
            IPAddress fallbackMask(255, 255, 255, 0);

            Ethernet.begin(mac,
                           fallbackIp,
                           fallbackDns,
                           fallbackGateway,
                           fallbackMask);
            netSettings_.setRuntimeState(NetworkSettings::RuntimeState::DhcpFallback);
        } else {
            Serial.println("ETH: DHCP OK");
            netSettings_.setRuntimeState(NetworkSettings::RuntimeState::DhcpOk);
        }
    } else {
        Serial.println("ETH: applying saved STATIC configuration");
        Ethernet.begin(mac,
                       NetworkSettings::toIp(cfg.ip),
                       NetworkSettings::toIp(cfg.dns),
                       NetworkSettings::toIp(cfg.gateway),
                       NetworkSettings::toIp(cfg.mask));
        netSettings_.setRuntimeState(NetworkSettings::RuntimeState::Static);
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

String WebServerApp::urlDecode(const String& value) {
    String out;
    out.reserve(value.length());
    for (size_t i = 0; i < value.length(); ++i) {
        char c = value[i];
        if (c == '+') {
            out += ' ';
        } else if (c == '%' && i + 2 < value.length()) {
            auto hex = [](char h) -> int {
                if (h >= '0' && h <= '9') return h - '0';
                if (h >= 'a' && h <= 'f') return h - 'a' + 10;
                if (h >= 'A' && h <= 'F') return h - 'A' + 10;
                return -1;
            };
            const int hi = hex(value[i + 1]);
            const int lo = hex(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
            } else {
                out += c;
            }
        } else {
            out += c;
        }
    }
    return out;
}

String WebServerApp::queryParam(const String& target, const char* key) {
    const int q = target.indexOf('?');
    if (q < 0) return String();
    String query = target.substring(q + 1);
    const String prefix = String(key) + "=";
    int pos = 0;
    while (pos < (int)query.length()) {
        int amp = query.indexOf('&', pos);
        if (amp < 0) amp = query.length();
        String part = query.substring(pos, amp);
        if (part.startsWith(prefix)) return urlDecode(part.substring(prefix.length()));
        pos = amp + 1;
    }
    return String();
}

String WebServerApp::jsonEscape(const String& value) {
    String out;
    out.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); ++i) {
        const char c = value[i];
        if (c == '\\' || c == '"') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return out;
}

String WebServerApp::scriptsJson() const {
    ScriptStore::EntryInfo entries[ScriptStore::MAX_SCRIPTS]{};
    const uint8_t count = store_.list(entries, ScriptStore::MAX_SCRIPTS);
    String body = "{\"ok\":true,\"maxScripts\":";
    body += String(ScriptStore::MAX_SCRIPTS);
    body += ",\"maxScriptBytes\":";
    body += String(ScriptStore::MAX_SCRIPT);
    body += ",\"scripts\":[";
    for (uint8_t i = 0; i < count; ++i) {
        if (i) body += ',';
        body += "{\"name\":\"";
        body += jsonEscape(entries[i].name);
        body += "\",\"length\":";
        body += String(entries[i].length);
        body += ",\"checksum\":";
        body += String(entries[i].checksum);
        body += '}';
    }
    body += "]}";
    return body;
}

void WebServerApp::handleClient(EthernetClient& client) {
    client.setTimeout(1000);
    String requestLine = client.readStringUntil('\n');
    requestLine.trim();

    const int sp1 = requestLine.indexOf(' ');
    const int sp2 = sp1 >= 0 ? requestLine.indexOf(' ', sp1 + 1) : -1;
    if (sp1 < 0 || sp2 < 0) {
        sendResponse(client, 400, "text/plain; charset=utf-8", "Bad request\n");
        return;
    }
    const String method = requestLine.substring(0, sp1);
    const String target = requestLine.substring(sp1 + 1, sp2);

    size_t contentLength = 0;
    while (client.connected()) {
        String line = client.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) break;
        if (line.startsWith("Content-Length:")) contentLength = (size_t)line.substring(15).toInt();
    }

    if (method == "GET" && (target == "/" || target == "/index.html")) {
        sendIndex(client);
        return;
    }

    if (method == "GET" && target == "/api/status") {
        IPAddress current = Ethernet.localIP();
        String body = "{\"ok\":true,\"ip\":\"";
        body += String(current[0]) + "." + String(current[1]) + "." + String(current[2]) + "." + String(current[3]);
        body += "\",\"luaRunning\":";
        body += scripts_.isRunning() ? "true" : "false";
        body += ",\"luaPending\":";
        body += scripts_.hasPending() ? "true" : "false";
        body += "}";
        sendResponse(client, 200, "application/json; charset=utf-8", body);
        return;
    }

    if (method == "GET" && target == "/api/output") {
        sendResponse(client, 200, "text/plain; charset=utf-8", scripts_.output());
        return;
    }

    if (method == "GET" && target == "/api/scripts") {
        sendResponse(client, 200, "application/json; charset=utf-8", scriptsJson());
        return;
    }

    if (method == "GET" && target.startsWith("/api/script?")) {
        const String name = queryParam(target, "name");
        String code, error;
        if (!store_.load(name, code, &error)) {
            sendResponse(client, 404, "text/plain; charset=utf-8", error + "\n");
            return;
        }
        sendResponse(client, 200, "text/plain; charset=utf-8", code);
        return;
    }

    if (method == "POST" && target.startsWith("/api/script?")) {
        if (contentLength > ScriptStore::MAX_SCRIPT) {
            sendResponse(client, 413, "text/plain; charset=utf-8", "Script too large for flash store\n");
            return;
        }
        const String name = queryParam(target, "name");
        const String code = readBody(client, contentLength);
        String error;
        if (!store_.save(name, code, &error)) {
            sendResponse(client, 400, "text/plain; charset=utf-8", error + "\n");
            return;
        }
        sendResponse(client, 200, "application/json; charset=utf-8", "{\"ok\":true}\n");
        return;
    }

    if (method == "DELETE" && target.startsWith("/api/script?")) {
        const String name = queryParam(target, "name");
        String error;
        if (!store_.remove(name, &error)) {
            sendResponse(client, 404, "text/plain; charset=utf-8", error + "\n");
            return;
        }
        sendResponse(client, 200, "application/json; charset=utf-8", "{\"ok\":true}\n");
        return;
    }

    if (method == "POST" && target == "/api/run") {
        if (contentLength > MAX_BODY) {
            sendResponse(client, 413, "text/plain; charset=utf-8", "Script too large\n");
            return;
        }
        String code = readBody(client, contentLength);
        if (!scripts_.submit(code)) {
            sendResponse(client, 409, "text/plain; charset=utf-8", "Lua busy or queue full\n");
            return;
        }
        sendResponse(client, 200, "text/plain; charset=utf-8", "QUEUED\n");
        return;
    }

    if (method == "POST" && target == "/api/stop") {
        scripts_.stop();
        sendResponse(client, 200, "text/plain; charset=utf-8", "STOP requested\n");
        return;
    }

    sendResponse(client, 404, "text/plain; charset=utf-8", "Not found\n");
}

void WebServerApp::sendIndex(EthernetClient& client) {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html; charset=utf-8");
    client.println("Cache-Control: no-store");
    client.print("Content-Length: ");
    client.println(strlen(WEB_INDEX_HTML));
    client.println("Connection: close");
    client.println();
    client.print(WEB_INDEX_HTML);
}

void WebServerApp::sendResponse(EthernetClient& client, int code, const char* contentType, const String& body) {
    client.print("HTTP/1.1 ");
    client.print(code);
    if (code == 200) client.println(" OK");
    else if (code == 400) client.println(" Bad Request");
    else if (code == 404) client.println(" Not Found");
    else if (code == 409) client.println(" Conflict");
    else if (code == 413) client.println(" Payload Too Large");
    else client.println(" Error");
    client.print("Content-Type: ");
    client.println(contentType);
    client.println("Cache-Control: no-store");
    client.print("Content-Length: ");
    client.println(body.length());
    client.println("Connection: close");
    client.println();
    client.print(body);
}
