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

constexpr GPIO_TypeDef* ADDR_PORT = GPIOD;
constexpr uint16_t ADDR_MASK = 0xFFFFu;

constexpr GPIO_TypeDef* DATA_PORT = GPIOE;
constexpr uint16_t DATA_MASK = 0xFFFFu;

// Control signals. Active levels are defined below.
constexpr GPIO_TypeDef* WR_PORT = GPIOC;
constexpr uint16_t WR_PIN = GPIO_PIN_6;      // PC6, timer-capable (TIM3_CH1/TIM8_CH1)

constexpr GPIO_TypeDef* STROBE_PORT = GPIOC;
constexpr uint16_t STROBE_PIN = GPIO_PIN_7;  // PC7, timer-capable

constexpr GPIO_TypeDef* READY_PORT = GPIOC;
constexpr uint16_t READY_PIN = GPIO_PIN_8;   // PC8, EXTI8

constexpr GPIO_TypeDef* IRQ_PORT = GPIOC;
constexpr uint16_t IRQ_PIN = GPIO_PIN_9;     // PC9, EXTI9

constexpr GPIO_TypeDef* CS_PORT = GPIOF;
constexpr uint16_t CS_PIN = GPIO_PIN_0;      // PF0

constexpr GPIO_TypeDef* ADDR_OE_PORT = GPIOF;
constexpr uint16_t ADDR_OE_PIN = GPIO_PIN_1; // PF1

constexpr GPIO_TypeDef* DATA_OE_PORT = GPIOF;
constexpr uint16_t DATA_OE_PIN = GPIO_PIN_2; // PF2

constexpr GPIO_TypeDef* DATA_DIR_PORT = GPIOF;
constexpr uint16_t DATA_DIR_PIN = GPIO_PIN_3; // PF3

// Reserved outputs for additional strobes/control lines requested by the module.
constexpr GPIO_TypeDef* AUX1_PORT = GPIOF;
constexpr uint16_t AUX1_PIN = GPIO_PIN_4;     // PF4
constexpr GPIO_TypeDef* AUX2_PORT = GPIOF;
constexpr uint16_t AUX2_PIN = GPIO_PIN_5;     // PF5

// Logic conventions for the external level-shifting bus transceivers.
// Change here if the selected transceiver has different polarity.
constexpr bool WR_ACTIVE_LOW = true;
constexpr bool CS_ACTIVE_LOW = true;
constexpr bool STROBE_ACTIVE_LOW = true;
constexpr bool ADDR_OE_ACTIVE_LOW = true;
constexpr bool DATA_OE_ACTIVE_LOW = true;

// DATA_DIR=true means STM32 -> analyzed module.
constexpr bool DATA_DIR_TO_MODULE_LEVEL = true;

} // namespace BoardPins
