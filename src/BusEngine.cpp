#include "BusEngine.h"

BusEngine& BusEngine::instance() {
    static BusEngine engine;
    return engine;
}

bool BusEngine::begin() {
    if (!queue_) queue_ = xQueueCreate(16, sizeof(Command*));
    if (!queue_) return false;

    if (!taskHandle_) {
        if (xTaskCreate(taskEntry, "BusTask", 3072, this, 5, &taskHandle_) != pdPASS) {
            taskHandle_ = nullptr;
            return false;
        }
    }
    return true;
}

bool BusEngine::write(uint16_t address, uint16_t data, TickType_t timeout) {
    Command cmd{Type::Write, address, data, 0, xTaskGetCurrentTaskHandle(), false};
    Command* ptr = &cmd;
    if (xQueueSend(queue_, &ptr, timeout) != pdPASS) return false;
    if (ulTaskNotifyTake(pdTRUE, timeout) == 0) return false;
    return cmd.ok;
}

bool BusEngine::read(uint16_t address, uint16_t& value, TickType_t timeout) {
    Command cmd{Type::Read, address, 0, 0, xTaskGetCurrentTaskHandle(), false};
    Command* ptr = &cmd;
    if (xQueueSend(queue_, &ptr, timeout) != pdPASS) return false;
    if (ulTaskNotifyTake(pdTRUE, timeout) == 0) return false;
    value = cmd.result;
    return cmd.ok;
}

void BusEngine::taskEntry(void* arg) {
    static_cast<BusEngine*>(arg)->taskLoop();
}

void BusEngine::taskLoop() {
    for (;;) {
        Command* cmd = nullptr;
        if (xQueueReceive(queue_, &cmd, portMAX_DELAY) == pdPASS && cmd) {
            busy_ = true;
            execute(*cmd);
            busy_ = false;
            xTaskNotifyGive(cmd->requester);
        }
    }
}

void BusEngine::execute(Command& cmd) {
    const uint16_t physicalAddress = invertAddress_ ? static_cast<uint16_t>(~cmd.address) : cmd.address;

    if (cmd.type == Type::Write) {
        const uint16_t physicalData = invertData_ ? static_cast<uint16_t>(~cmd.data) : cmd.data;
        (void)physicalAddress;
        (void)physicalData;
        // TODO: здесь будет HardwareBus: ADDR/DATA + CS/WR через GPIO/TIM/DMA.
        cmd.ok = true;
        return;
    }

    (void)physicalAddress;
    uint16_t physicalValue = 0xFFFF; // TODO: чтение DATA[15:0] с аппаратной шины.
    cmd.result = invertData_ ? static_cast<uint16_t>(~physicalValue) : physicalValue;
    cmd.ok = true;
}

void BusEngine::setReadyFromISR(bool state) {
    readyState_ = state;
}

void BusEngine::setIrqFromISR(bool state) {
    irqState_ = state;
}
