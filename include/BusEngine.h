#pragma once

#include <Arduino.h>
#include <STM32FreeRTOS.h>

class BusEngine {
public:
    static BusEngine& instance();

    bool begin();
    bool write(uint16_t address, uint16_t data, TickType_t timeout = pdMS_TO_TICKS(1000));
    bool read(uint16_t address, uint16_t& value, TickType_t timeout = pdMS_TO_TICKS(1000));

    void setInvertAddress(bool enabled) { invertAddress_ = enabled; }
    void setInvertData(bool enabled) { invertData_ = enabled; }
    bool invertAddress() const { return invertAddress_; }
    bool invertData() const { return invertData_; }

    bool ready() const { return readyState_; }
    bool irq() const { return irqState_; }
    bool busy() const { return busy_; }

    void setReadyFromISR(bool state);
    void setIrqFromISR(bool state);

private:
    BusEngine() = default;

    enum class Type : uint8_t { Write, Read };

    struct Command {
        Type type;
        uint16_t address;
        uint16_t data;
        uint16_t result;
        TaskHandle_t requester;
        bool ok;
    };

    QueueHandle_t queue_ = nullptr;
    TaskHandle_t taskHandle_ = nullptr;
    volatile bool busy_ = false;
    volatile bool readyState_ = false;
    volatile bool irqState_ = false;
    volatile bool invertAddress_ = false;
    volatile bool invertData_ = false;

    static void taskEntry(void* arg);
    void taskLoop();
    void execute(Command& cmd);
};
