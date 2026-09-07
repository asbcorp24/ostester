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

void readyExtiCallback() {
    const bool state = (BoardPins::READY_PORT->IDR & BoardPins::READY_PIN) != 0;
    BusEngine::instance().setReadyFromISR(state);
}

void irqExtiCallback() {
    const bool state = (BoardPins::IRQ_PORT->IDR & BoardPins::IRQ_PIN) != 0;
    BusEngine::instance().setIrqFromISR(state);
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

    readyState_ = ready();
    irqState_ = irq();
    return true;
}

void BusEngine::initHardware() {
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

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

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    initPulseTimer();
    initEventInputs();

    emergencyStop();
    setAddress(0x0000);
    setData(0x0000);
}

void BusEngine::initPulseTimer() {
    __HAL_RCC_TIM3_CLK_ENABLE();

    RCC_ClkInitTypeDef clk{};
    uint32_t flashLatency = 0;
    HAL_RCC_GetClockConfig(&clk, &flashLatency);

    uint32_t timerClock = HAL_RCC_GetPCLK1Freq();
    if (clk.APB1CLKDivider != RCC_HCLK_DIV1) timerClock *= 2u;

    TIM3->CR1 = 0;
    TIM3->PSC = (timerClock / 1000000u) - 1u; // 1 MHz -> 1 timer tick = 1 us
    TIM3->ARR = 1u;
    TIM3->CNT = 0u;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->SR = 0u;
}

void BusEngine::initEventInputs() {
    // Arduino STM32 maps these to PC8/PC9 EXTI lines; callback executes in ISR context.
    attachInterrupt(digitalPinToInterrupt(PC8), readyExtiCallback, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PC9), irqExtiCallback, CHANGE);
}

void BusEngine::setWriteTimingUs(uint32_t setupUs, uint32_t pulseUs, uint32_t holdUs) {
    setupUs_ = setupUs;
    pulseUs_ = pulseUs;
    holdUs_ = holdUs;
}

bool BusEngine::write(uint16_t address, uint16_t data, TickType_t timeout) {
    Command cmd{Type::Write, address, data, 0, 0, xTaskGetCurrentTaskHandle(), false};
    Command* ptr = &cmd;
    if (xQueueSend(queue_, &ptr, timeout) != pdPASS) return false;
    if (ulTaskNotifyTake(pdTRUE, timeout) == 0) return false;
    return cmd.ok;
}

bool BusEngine::read(uint16_t address, uint16_t& value, TickType_t timeout) {
    Command cmd{Type::Read, address, 0, 0, 0, xTaskGetCurrentTaskHandle(), false};
    Command* ptr = &cmd;
    if (xQueueSend(queue_, &ptr, timeout) != pdPASS) return false;
    if (ulTaskNotifyTake(pdTRUE, timeout) == 0) return false;
    value = cmd.result;
    return cmd.ok;
}

bool BusEngine::waitReady(uint32_t timeoutUs, TickType_t queueTimeout) {
    Command cmd{Type::WaitReady, 0, 0, 0, timeoutUs, xTaskGetCurrentTaskHandle(), false};
    Command* ptr = &cmd;
    if (xQueueSend(queue_, &ptr, queueTimeout) != pdPASS) return false;

    TickType_t replyTimeout = queueTimeout;
    if (timeoutUs >= 1000u) {
        replyTimeout += pdMS_TO_TICKS((timeoutUs + 999u) / 1000u + 1u);
    }

    if (ulTaskNotifyTake(pdTRUE, replyTimeout) == 0) return false;
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
    if (cmd.type == Type::WaitReady) {
        cmd.ok = waitReadyInBusTask(cmd.timeoutUs);
        return;
    }

    const uint16_t physicalAddress = invertAddress_
        ? static_cast<uint16_t>(~cmd.address)
        : cmd.address;

    if (cmd.type == Type::Write) {
        const uint16_t physicalData = invertData_
            ? static_cast<uint16_t>(~cmd.data)
            : cmd.data;

        setDataEnabled(false);
        setDataDirectionToModule(true);
        setDataOutput();

        setAddress(physicalAddress);
        setData(physicalData);

        setAddressEnabled(true);
        setDataEnabled(true);

        delayUsPrecise(setupUs_);

        setCs(true);
        pulseWrTimer(pulseUs_);

        delayUsPrecise(holdUs_);
        setCs(false);

        cmd.ok = true;
        return;
    }

    setDataEnabled(false);
    setAddress(physicalAddress);
    setAddressEnabled(true);
    setDataInput();
    setDataDirectionToModule(false);
    setDataEnabled(true);

    delayUsPrecise(setupUs_);
    setCs(true);
    timerDelayUs(pulseUs_);
    uint16_t physicalValue = sampleData();
    setCs(false);
    delayUsPrecise(holdUs_);
    setDataEnabled(false);

    cmd.result = invertData_
        ? static_cast<uint16_t>(~physicalValue)
        : physicalValue;
    cmd.ok = true;
}

bool BusEngine::waitReadyInBusTask(uint32_t timeoutUs) {
    if (readyState_ || ready()) return true;
    if (timeoutUs == 0) return false;

    // For sub-tick waits we keep BusTask deterministic and check the DWT deadline.
    // EXTI still updates readyState_ asynchronously.
    if (timeoutUs < 1000u) {
        const uint32_t cyclesPerUs = SystemCoreClock / 1000000u;
        const uint32_t deadline = cyclesPerUs * timeoutUs;
        const uint32_t start = DWT->CYCCNT;
        while (static_cast<uint32_t>(DWT->CYCCNT - start) < deadline) {
            if (readyState_ || ready()) return true;
        }
        return readyState_ || ready();
    }

    const TickType_t ticks = pdMS_TO_TICKS((timeoutUs + 999u) / 1000u);
    const TickType_t startTick = xTaskGetTickCount();

    for (;;) {
        if (readyState_ || ready()) return true;

        const TickType_t elapsed = xTaskGetTickCount() - startTick;
        if (elapsed >= ticks) return false;

        uint32_t events = 0;
        const TickType_t remain = ticks - elapsed;
        xTaskNotifyWait(0u, EVENT_READY | EVENT_IRQ, &events, remain);
        if ((events & EVENT_READY) && (readyState_ || ready())) return true;
    }
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
    BoardPins::DATA_PORT->MODER = 0x55555555u;
    BoardPins::DATA_PORT->OTYPER = 0x00000000u;
    BoardPins::DATA_PORT->PUPDR = 0x00000000u;
    BoardPins::DATA_PORT->OSPEEDR = 0xFFFFFFFFu;
}

void BusEngine::setDataInput() {
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

void BusEngine::timerDelayUs(uint32_t us) {
    if (us == 0) return;

    // TIM3 is configured at 1 MHz. OPM clears CEN automatically on update.
    TIM3->CR1 = TIM_CR1_OPM;
    TIM3->PSC = TIM3->PSC;
    TIM3->ARR = us - 1u;
    TIM3->CNT = 0u;
    TIM3->SR = 0u;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->SR = 0u;
    TIM3->CR1 |= TIM_CR1_CEN;

    while ((TIM3->SR & TIM_SR_UIF) == 0u) {
        __NOP();
    }
    TIM3->SR &= ~TIM_SR_UIF;
}

void BusEngine::pulseWrTimer(uint32_t us) {
    setWr(true);
    timerDelayUs(us == 0 ? 1u : us);
    setWr(false);
}

void BusEngine::pulseStrobeTimer(uint32_t us) {
    setStrobe(true);
    timerDelayUs(us == 0 ? 1u : us);
    setStrobe(false);
}

bool BusEngine::ready() const {
    return (BoardPins::READY_PORT->IDR & BoardPins::READY_PIN) != 0;
}

bool BusEngine::irq() const {
    return (BoardPins::IRQ_PORT->IDR & BoardPins::IRQ_PIN) != 0;
}

void BusEngine::emergencyStop() {
    TIM3->CR1 &= ~TIM_CR1_CEN;
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
    if (!taskHandle_) return;

    BaseType_t higherPriorityTaskWoken = pdFALSE;
    xTaskNotifyFromISR(taskHandle_, EVENT_READY, eSetBits, &higherPriorityTaskWoken);
    portYIELD_FROM_ISR(higherPriorityTaskWoken);
}

void BusEngine::setIrqFromISR(bool state) {
    irqState_ = state;
    if (!taskHandle_) return;

    BaseType_t higherPriorityTaskWoken = pdFALSE;
    xTaskNotifyFromISR(taskHandle_, EVENT_IRQ, eSetBits, &higherPriorityTaskWoken);
    portYIELD_FROM_ISR(higherPriorityTaskWoken);
}
