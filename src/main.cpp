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
    // OLED is the first subsystem and shows every boot stage synchronously.
    localUi.begin();

    localUi.showBootStage("NET CFG");
    networkSettings.begin();

    localUi.showBootStage("ETH BEGIN");
    web.begin();
    localUi.showBootStage("ETH OK");

    localUi.showBootStage("BUS BEGIN");
    BusEngine::instance().begin();

    localUi.showBootStage("LUA BEGIN");
    scripts.begin();

    localUi.showBootStage("TASKS");
    if (xTaskCreate(webTask, "WebTask", 4096, nullptr, 3, &webTaskHandle) != pdPASS) {
        localUi.showBootStage("WEB TASK ERR");
        while (true) delay(1000);
    }

    if (xTaskCreate(uiTask, "UiTask", 2048, nullptr, 2, &uiTaskHandle) != pdPASS) {
        localUi.showBootStage("UI TASK ERR");
        while (true) delay(1000);
    }

    localUi.showBootStage("RTOS START");
    vTaskStartScheduler();
}

void loop() {
    // Not used after vTaskStartScheduler().
}
