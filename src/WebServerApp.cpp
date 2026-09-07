#include "WebServerApp.h"
#include "generated_web.h"

WebServerApp::WebServerApp(ScriptEngine& scripts) : scripts_(scripts) {}

bool WebServerApp::begin() {
    byte mac[] = {0x02, 0xF7, 0x67, 0x01, 0x00, 0x01};

    if (Ethernet.begin(mac) == 0) {
        IPAddress ip(192, 168, 1, 77);
        IPAddress dns(192, 168, 1, 1);
        IPAddress gateway(192, 168, 1, 1);
        IPAddress subnet(255, 255, 255, 0);
        Ethernet.begin(mac, ip, dns, gateway, subnet);
    }

    delay(250);
    server_.begin();
    return true;
}

void WebServerApp::loop() {
    EthernetClient client = server_.available();
    if (!client) return;
    handleClient(client);
    delay(1);
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
    }
    return body;
}

void WebServerApp::handleClient(EthernetClient& client) {
    client.setTimeout(1000);
    String requestLine = client.readStringUntil('\n');
    requestLine.trim();

    size_t contentLength = 0;
    while (client.connected()) {
        String line = client.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) break;
        if (line.startsWith("Content-Length:")) {
            contentLength = (size_t)line.substring(15).toInt();
        }
    }

    if (requestLine.startsWith("GET / ") || requestLine.startsWith("GET /index.html ")) {
        sendIndex(client);
        return;
    }

    if (requestLine.startsWith("GET /api/status ")) {
        IPAddress current = Ethernet.localIP();
        String body = "{\"ok\":true,\"ip\":\"";
        body += String(current[0]) + "." + String(current[1]) + "." + String(current[2]) + "." + String(current[3]);
        body += "\",\"luaRunning\":";
        body += scripts_.isRunning() ? "true" : "false";
        body += "}";
        sendResponse(client, 200, "application/json; charset=utf-8", body);
        return;
    }

    if (requestLine.startsWith("POST /api/run ")) {
        if (contentLength > MAX_BODY) {
            sendResponse(client, 413, "text/plain; charset=utf-8", "Script too large");
            return;
        }

        String code = readBody(client, contentLength);
        String output;
        const bool ok = scripts_.run(code, output);
        String body = ok ? "OK\n" : "ERROR\n";
        body += output;
        sendResponse(client, ok ? 200 : 400, "text/plain; charset=utf-8", body);
        return;
    }

    if (requestLine.startsWith("POST /api/stop ")) {
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
    client.println(code == 200 ? " OK" : (code == 400 ? " Bad Request" : (code == 404 ? " Not Found" : " Error")));
    client.print("Content-Type: ");
    client.println(contentType);
    client.println("Cache-Control: no-store");
    client.print("Content-Length: ");
    client.println(body.length());
    client.println("Connection: close");
    client.println();
    client.print(body);
}
