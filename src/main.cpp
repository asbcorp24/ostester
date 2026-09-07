#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include "BusEngine.h"
#include "ScriptEngine.h"
#include "WebServerApp.h"

ScriptEngine scripts;
WebServerApp web(scripts);

static TaskHandle_t webTaskHandle = nullptr;

static void webTask(void*) {
    for (;;) {
        web.loop();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void setup() {
    Serial.begin(115200);
    delay(300);

    Serial.println();
    Serial.println("OSTester boot / FreeRTOS");

    if (!BusEngine::instance().begin()) {
        Serial.println("BusTask init failed");
    } else {
        Serial.println("BusTask ready");
    }

    if (!scripts.begin()) {
        Serial.println("Lua FreeRTOS task init failed");
    } else {
        Serial.println("LuaTask ready");
    }

    web.begin();

    IPAddress ip = web.ip();
    Serial.print("HTTP: http://");
    Serial.print(ip);
    Serial.println("/");

    if (xTaskCreate(webTask, "WebTask", 4096, nullptr, 3, &webTaskHandle) != pdPASS) {
        Serial.println("WebTask create failed");
        while (true) delay(1000);
    }

    Serial.println("Starting FreeRTOS scheduler");
    vTaskStartScheduler();
}

void loop() {
    // Не используется: после vTaskStartScheduler() работают задачи FreeRTOS.
}
