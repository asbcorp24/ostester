#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include "BusEngine.h"
#include "ScriptEngine.h"
#include "WebServerApp.h"
#include "NetworkSettings.h"
#include "LocalUi.h"

NetworkSettings networkSettings;
ScriptEngine scripts;
WebServerApp web(scripts, networkSettings);
LocalUi localUi(networkSettings);

static TaskHandle_t webTaskHandle = nullptr;
static TaskHandle_t uiTaskHandle = nullptr;

static void webTask(void*) {
    for (;;) {
        Ethernet.schedule();
        web.loop();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void uiTask(void*) {
    for (;;) {
        localUi.loop();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void setup() {
    Serial.begin(115200);
    delay(300);

    Serial.println();
    Serial.println("OSTester boot / FreeRTOS");

    networkSettings.begin();

    // OLED first, so diagnostics are visible immediately.
    if (!localUi.begin()) {
        Serial.println("OLED init failed");
    } else {
        Serial.println("OLED ready");
    }

    // Ethernet MUST start before BusEngine/Lua so another subsystem cannot
    // prevent network initialization.
    Serial.println("Starting Ethernet...");
    web.begin();
    Serial.println("Ethernet begin returned");

    IPAddress ip = web.ip();
    Serial.print("HTTP: http://");
    Serial.print(ip);
    Serial.println("/");

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

    if (xTaskCreate(webTask, "WebTask", 4096, nullptr, 3, &webTaskHandle) != pdPASS) {
        Serial.println("WebTask create failed");
        while (true) delay(1000);
    }

    if (xTaskCreate(uiTask, "UiTask", 2048, nullptr, 2, &uiTaskHandle) != pdPASS) {
        Serial.println("UiTask create failed");
        while (true) delay(1000);
    }

    Serial.println("Starting FreeRTOS scheduler");
    vTaskStartScheduler();
}

void loop() {
    // Не используется: после vTaskStartScheduler() работают задачи FreeRTOS.
}
