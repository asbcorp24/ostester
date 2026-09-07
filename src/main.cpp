#include <Arduino.h>
#include "ScriptEngine.h"
#include "WebServerApp.h"

ScriptEngine scripts;
WebServerApp web(scripts);

void setup() {
    Serial.begin(115200);
    delay(300);

    Serial.println();
    Serial.println("OSTester boot");

    if (!scripts.begin()) {
        Serial.println("Lua init failed");
    } else {
        Serial.println("Lua ready");
    }

    web.begin();

    IPAddress ip = web.ip();
    Serial.print("HTTP: http://");
    Serial.print(ip);
    Serial.println("/");
}

void loop() {
    web.loop();
}
