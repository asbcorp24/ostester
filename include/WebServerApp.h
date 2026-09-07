#pragma once

#include <Arduino.h>
#include <STM32Ethernet.h>
#include "ScriptEngine.h"

class WebServerApp {
public:
    explicit WebServerApp(ScriptEngine& scripts);
    bool begin();
    void loop();
    IPAddress ip() const { return Ethernet.localIP(); }

private:
    EthernetServer server_{80};
    ScriptEngine& scripts_;

    static constexpr size_t MAX_BODY = 16384;

    void handleClient(EthernetClient& client);
    void sendResponse(EthernetClient& client, int code, const char* contentType, const String& body);
    void sendIndex(EthernetClient& client);
    String readBody(EthernetClient& client, size_t contentLength);
};
