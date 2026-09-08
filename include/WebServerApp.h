#pragma once

#include <Arduino.h>
#include <STM32Ethernet.h>
#include "ScriptEngine.h"
#include "ScriptStore.h"
#include "NetworkSettings.h"

class WebServerApp {
public:
    WebServerApp(ScriptEngine& scripts, NetworkSettings& netSettings);
    bool begin();
    void loop();
    IPAddress ip() const { return Ethernet.localIP(); }

private:
    EthernetServer server_{80};
    ScriptEngine& scripts_;
    NetworkSettings& netSettings_;
    ScriptStore store_;

    static constexpr size_t MAX_BODY = 16384;

    void handleClient(EthernetClient& client);
    void sendResponse(EthernetClient& client, int code, const char* contentType, const String& body);
    void sendIndex(EthernetClient& client);
    String readBody(EthernetClient& client, size_t contentLength);
    static String urlDecode(const String& value);
    static String queryParam(const String& target, const char* key);
    static String jsonEscape(const String& value);
    String scriptsJson() const;
};
