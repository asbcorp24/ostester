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

static bool isZeroIp(const IPAddress& ip) {
    return ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0;
}

void setup() {
    networkSettings.begin();

    BusEngine::instance().begin();
    scripts.begin();

    // Keep the previously working Ethernet startup path.
    web.begin();

    const auto& cfg = networkSettings.config();
    IPAddress currentIp = Ethernet.localIP();

    if (cfg.dhcp) {
        if (isZeroIp(currentIp)) {
            // DHCP did not produce an address: guarantee a usable static fallback.
            byte mac[] = {0x02, 0xF7, 0x67, 0x01, 0x00, 0x01};
            Ethernet.begin(
                mac,
                NetworkSettings::toIp(cfg.ip),
                NetworkSettings::toIp(cfg.dns),
                NetworkSettings::toIp(cfg.gateway),
                NetworkSettings::toIp(cfg.mask)
            );
            delay(250);
            networkSettings.setRuntimeState(NetworkSettings::RuntimeState::DhcpFallback);
        } else {
            networkSettings.setRuntimeState(NetworkSettings::RuntimeState::DhcpOk);
        }
    } else {
        networkSettings.setRuntimeState(NetworkSettings::RuntimeState::Static);
    }

    // OLED starts after Ethernet and displays the actual assigned/fallback IP.
    localUi.begin();

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
