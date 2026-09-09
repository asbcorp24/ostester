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
    // Restore the original working initialization order.
    networkSettings.begin();

    BusEngine::instance().begin();
    scripts.begin();

    // Ethernet startup is exactly the old working WebServerApp path.
    web.begin();

    // OLED is passive: it starts only after Ethernet and only displays state/IP.
    localUi.begin();

    const auto& cfg = networkSettings.config();
    IPAddress ip = Ethernet.localIP();
    if (ip[0] || ip[1] || ip[2] || ip[3]) {
        networkSettings.setRuntimeState(cfg.dhcp
            ? NetworkSettings::RuntimeState::DhcpOk
            : NetworkSettings::RuntimeState::Static);
    }

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
