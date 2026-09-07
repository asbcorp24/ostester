#include "BusEngine.h"
#include "BoardPins.h"

namespace {

inline void writePin(GPIO_TypeDef* port, uint16_t pin, bool high) {
    port->BSRR = high ? pin : (static_cast<uint32_t>(pin) << 16U);
}

inline bool activeLevel(bool active, bool activeLow) {
    return activeLow ? !active : active;
}

void initOutput(GPIO_TypeDef* port, uint32_t pins) {
    GPIO_InitTypeDef gpio{};
    gpio.Pin = pins;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(port, &gpio);
}

void initInput(GPIO_TypeDef* port, uint32_t pins) {
    GPIO_InitTypeDef gpio{};
    gpio.Pin = pins;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(port, &gpio);
}

} // namespace

BusEngine& BusEngine::instance() {
    static BusEngine engine;
    return engine;
}

bool BusEngine::begin() {
    initHardware();

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

void BusEngine::initHardware() {
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    // Both 16-bit buses are dedicated whole GPIO ports.
    initOutput(BoardPins::ADDR_PORT, BoardPins::ADDR_MASK);
    initOutput(BoardPins::DATA_PORT, BoardPins::DATA_MASK);

    initOutput(BoardPins::WR_PORT, BoardPins::WR_PIN);
    initOutput(BoardPins::STROBE_PORT, BoardPins::STROBE_PIN);
    initOutput(BoardPins::CS_PORT, BoardPins::CS_PIN);
    initOutput(BoardPins::ADDR_OE_PORT, BoardPins::ADDR_OE_PIN);
    initOutput(BoardPins::DATA_OE_PORT, BoardPins::DATA_OE_PIN);
    initOutput(BoardPins::DATA_DIR_PORT, BoardPins::DATA_DIR_PIN);
    initOutput(BoardPins::AUX1_PORT, BoardPins::AUX1_PIN);
    initOutput(BoardPins::AUX2_PORT, BoardPins::AUX2_PIN);

    initInput(BoardPins::READY_PORT, BoardPins::READY_PIN);
    initInput(BoardPins::IRQ_PORT, BoardPins::IRQ_PIN);

    // DWT gives sub-RTOS-tick microsecond timing for the first hardware stage.
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    emergencyStop();
    setAddress(0x0000);
    setData(0x0000);
}

void BusEngine::setWriteTimingUs(uint32_t setupUs, uint32_t pulseUs, uint32_t holdUs) {
    setupUs_ = setupUs;
    pulseUs_ = pulseUs;
    holdUs_ = holdUs;
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
    const uint16_t physicalAddress = invertAddress_
        ? static_cast<uint16_t>(~cmd.address)
        : cmd.address;

    if (cmd.type == Type::Write) {
        const uint16_t physicalData = invertData_
            ? static_cast<uint16_t>(~cmd.data)
            : cmd.data;

        // 1) Prepare safe direction before enabling the external transceiver.
        setDataEnabled(false);
        setDataDirectionToModule(true);
        setDataOutput();

        // 2) Entire 16-bit words are placed on the buses with one ODR write each.
        setAddress(physicalAddress);
        setData(physicalData);

        // 3) Enable address/data level shifters.
        setAddressEnabled(true);
        setDataEnabled(true);

        // 4) Address/data setup time.
        delayUsPrecise(setupUs_);

        // 5) Select module and generate write pulse.
        setCs(true);
        setWr(true);
        delayUsPrecise(pulseUs_);
        setWr(false);

        // 6) Hold bus values after the write edge.
        delayUsPrecise(holdUs_);
        setCs(false);

        cmd.ok = true;
        return;
    }

    // Generic data-bus sampling cycle. Exact read strobe semantics will be
    // adapted once the analyzed module's read protocol is defined.
    setDataEnabled(false);
    setAddress(physicalAddress);
    setAddressEnabled(true);
    setDataInput();
    setDataDirectionToModule(false);
    setDataEnabled(true);

    delayUsPrecise(setupUs_);
    setCs(true);
    delayUsPrecise(pulseUs_);
    uint16_t physicalValue = sampleData();
    setCs(false);
    delayUsPrecise(holdUs_);
    setDataEnabled(false);

    cmd.result = invertData_
        ? static_cast<uint16_t>(~physicalValue)
        : physicalValue;
    cmd.ok = true;
}

void BusEngine::setAddress(uint16_t value) {
    BoardPins::ADDR_PORT->ODR = value;
}

void BusEngine::setData(uint16_t value) {
    BoardPins::DATA_PORT->ODR = value;
}

uint16_t BusEngine::sampleData() const {
    return static_cast<uint16_t>(BoardPins::DATA_PORT->IDR & 0xFFFFu);
}

void BusEngine::setDataOutput() {
    // GPIO mode 01 = general purpose output for every PE0..PE15.
    BoardPins::DATA_PORT->MODER = 0x55555555u;
    BoardPins::DATA_PORT->OTYPER = 0x00000000u;
    BoardPins::DATA_PORT->PUPDR = 0x00000000u;
    BoardPins::DATA_PORT->OSPEEDR = 0xFFFFFFFFu;
}

void BusEngine::setDataInput() {
    // GPIO mode 00 = input for every PE0..PE15.
    BoardPins::DATA_PORT->MODER = 0x00000000u;
    BoardPins::DATA_PORT->PUPDR = 0x00000000u;
}

void BusEngine::setDataDirectionToModule(bool toModule) {
    const bool high = toModule
        ? BoardPins::DATA_DIR_TO_MODULE_LEVEL
        : !BoardPins::DATA_DIR_TO_MODULE_LEVEL;
    writePin(BoardPins::DATA_DIR_PORT, BoardPins::DATA_DIR_PIN, high);
}

void BusEngine::setAddressEnabled(bool enabled) {
    writePin(BoardPins::ADDR_OE_PORT, BoardPins::ADDR_OE_PIN,
             activeLevel(enabled, BoardPins::ADDR_OE_ACTIVE_LOW));
}

void BusEngine::setDataEnabled(bool enabled) {
    writePin(BoardPins::DATA_OE_PORT, BoardPins::DATA_OE_PIN,
             activeLevel(enabled, BoardPins::DATA_OE_ACTIVE_LOW));
}

void BusEngine::setCs(bool active) {
    writePin(BoardPins::CS_PORT, BoardPins::CS_PIN,
             activeLevel(active, BoardPins::CS_ACTIVE_LOW));
}

void BusEngine::setWr(bool active) {
    writePin(BoardPins::WR_PORT, BoardPins::WR_PIN,
             activeLevel(active, BoardPins::WR_ACTIVE_LOW));
}

void BusEngine::setStrobe(bool active) {
    writePin(BoardPins::STROBE_PORT, BoardPins::STROBE_PIN,
             activeLevel(active, BoardPins::STROBE_ACTIVE_LOW));
}

void BusEngine::delayUsPrecise(uint32_t us) const {
    if (us == 0) return;
    const uint32_t cyclesPerUs = SystemCoreClock / 1000000u;
    const uint32_t waitCycles = cyclesPerUs * us;
    const uint32_t start = DWT->CYCCNT;
    while (static_cast<uint32_t>(DWT->CYCCNT - start) < waitCycles) {
        __NOP();
    }
}

bool BusEngine::ready() const {
    return (BoardPins::READY_PORT->IDR & BoardPins::READY_PIN) != 0;
}

bool BusEngine::irq() const {
    return (BoardPins::IRQ_PORT->IDR & BoardPins::IRQ_PIN) != 0;
}

void BusEngine::emergencyStop() {
    setWr(false);
    setStrobe(false);
    setCs(false);
    setDataEnabled(false);
    setAddressEnabled(false);
    setDataDirectionToModule(false);
    setDataInput();
}

void BusEngine::setReadyFromISR(bool state) {
    readyState_ = state;
}

void BusEngine::setIrqFromISR(bool state) {
    irqState_ = state;
}
