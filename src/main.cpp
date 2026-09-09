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
    // OLED is the very first subsystem so boot status is visible immediately.
    localUi.begin();

    networkSettings.begin();

    // Ethernet starts before BusEngine/Lua.
    web.begin();

    BusEngine::instance().begin();
    scripts.begin();

    if (xTaskCreate(webTask, "WebTask", 4096, nullptr, 3, &webTaskHandle) != pdPASS) {
        while (true) delay(1000);
    }

    if (xTaskCreate(uiTask, "UiTask", 2048, nullptr, 2, &uiTaskHandle) != pdPASS) {
        while (true) delay(1000);
    }

    vTaskStartScheduler();
}

void loop() {
    // Not used after vTaskStartScheduler().
}
