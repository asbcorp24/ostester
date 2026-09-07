#pragma once

#include <Arduino.h>
#include "stm32f7xx_hal.h"

/*
 * OSTester / NUCLEO-F767ZI bus assignment
 *
 * Address bus: GPIOD[15:0] -> ADDR[15:0]
 * Data bus:    GPIOE[15:0] <-> DATA[15:0]
 *
 * IMPORTANT FOR NUCLEO-F767ZI:
 * PD8/PD9 are connected to the ST-LINK virtual COM port by default.
 * To use the complete GPIOD port on the Morpho headers:
 *   - SB5, SB6: OPEN (disconnect ST-LINK USART)
 *   - SB7, SB4: CLOSED (keep PD8/PD9 connected to Morpho)
 *
 * On-board RMII Ethernet uses PA1, PA2, PA7, PB13, PC1, PC4, PC5,
 * PG11 and PG13, so the bus assignment below does not overlap Ethernet.
 */
namespace BoardPins {

// GPIOx CMSIS macros are address casts and are not valid C++ constexpr pointers.
// Use inline variables so they are still centralized in one header without
// requiring constant-expression evaluation.
inline GPIO_TypeDef* const ADDR_PORT = GPIOD;
constexpr uint16_t ADDR_MASK = 0xFFFFu;

inline GPIO_TypeDef* const DATA_PORT = GPIOE;
constexpr uint16_t DATA_MASK = 0xFFFFu;

inline GPIO_TypeDef* const WR_PORT = GPIOC;
constexpr uint16_t WR_PIN = GPIO_PIN_6;

inline GPIO_TypeDef* const STROBE_PORT = GPIOC;
constexpr uint16_t STROBE_PIN = GPIO_PIN_7;

inline GPIO_TypeDef* const READY_PORT = GPIOC;
constexpr uint16_t READY_PIN = GPIO_PIN_8;

inline GPIO_TypeDef* const IRQ_PORT = GPIOC;
constexpr uint16_t IRQ_PIN = GPIO_PIN_9;

inline GPIO_TypeDef* const CS_PORT = GPIOF;
constexpr uint16_t CS_PIN = GPIO_PIN_0;

inline GPIO_TypeDef* const ADDR_OE_PORT = GPIOF;
constexpr uint16_t ADDR_OE_PIN = GPIO_PIN_1;

inline GPIO_TypeDef* const DATA_OE_PORT = GPIOF;
constexpr uint16_t DATA_OE_PIN = GPIO_PIN_2;

inline GPIO_TypeDef* const DATA_DIR_PORT = GPIOF;
constexpr uint16_t DATA_DIR_PIN = GPIO_PIN_3;

inline GPIO_TypeDef* const AUX1_PORT = GPIOF;
constexpr uint16_t AUX1_PIN = GPIO_PIN_4;
inline GPIO_TypeDef* const AUX2_PORT = GPIOF;
constexpr uint16_t AUX2_PIN = GPIO_PIN_5;

constexpr bool WR_ACTIVE_LOW = true;
constexpr bool CS_ACTIVE_LOW = true;
constexpr bool STROBE_ACTIVE_LOW = true;
constexpr bool ADDR_OE_ACTIVE_LOW = true;
constexpr bool DATA_OE_ACTIVE_LOW = true;
constexpr bool DATA_DIR_TO_MODULE_LEVEL = true;

} // namespace BoardPins
