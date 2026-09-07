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

    void setWriteTimingUs(uint32_t setupUs, uint32_t pulseUs, uint32_t holdUs);
    uint32_t setupUs() const { return setupUs_; }
    uint32_t pulseUs() const { return pulseUs_; }
    uint32_t holdUs() const { return holdUs_; }

    bool ready() const;
    bool irq() const;
    bool busy() const { return busy_; }

    void emergencyStop();
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

    volatile uint32_t setupUs_ = 1;
    volatile uint32_t pulseUs_ = 2;
    volatile uint32_t holdUs_ = 1;

    static void taskEntry(void* arg);
    void taskLoop();
    void execute(Command& cmd);

    void initHardware();
    void setAddress(uint16_t value);
    void setData(uint16_t value);
    uint16_t sampleData() const;
    void setDataOutput();
    void setDataInput();
    void setDataDirectionToModule(bool toModule);
    void setAddressEnabled(bool enabled);
    void setDataEnabled(bool enabled);
    void setCs(bool active);
    void setWr(bool active);
    void setStrobe(bool active);
    void delayUsPrecise(uint32_t us) const;
};
