# OSTester

Анализатор/формирователь 16-разрядной шины для NUCLEO-F767ZI.

## Цель

Устройство подключается между STM32 и анализируемым модулем через управляемые шинные формирователи 3.3/5 В.

Поддерживаемые линии:

- DATA[15:0] — двунаправленная шина, 3-state;
- ADDR[15:0] — однонаправленная шина;
- WR — запись;
- CS — выбор кристалла;
- STROBE — дополнительный строб;
- READY — готовность периферии;
- IRQ — прерывание от периферии;
- DATA_OE / DATA_DIR — управление формирователем данных;
- ADDR_OE — управление формирователем адреса.

## Архитектура FreeRTOS

```text
Browser
   |
Ethernet / HTTP
   |
WebTask (priority 3)
   |
LuaTask (priority 4)
   |
BusQueue
   |
BusTask (priority 5)
   |
GPIO + DWT + TIM3 + EXTI
   |
3.3V <-> 5V level/bus transceivers
   |
Analyzed module
```

Веб, Lua и аппаратная шина работают независимо. HTTP не ждёт завершения Lua-программы.

## Принятая распиновка NUCLEO-F767ZI

### Шина адреса

ADDR0..ADDR15 = PD0..PD15.

Физическая выдача слова:

```cpp
GPIOD->ODR = address;
```

### Шина данных

DATA0..DATA15 = PE0..PE15.

Выдача данных:

```cpp
GPIOE->ODR = data;
```

Чтение данных:

```cpp
uint16_t data = (uint16_t)GPIOE->IDR;
```

### Управляющие линии

| Сигнал | STM32 | Назначение |
|---|---|---|
| WR | PC6 | импульс записи |
| STROBE | PC7 | дополнительный аппаратный строб |
| READY | PC8 | EXTI8, готовность периферии |
| IRQ | PC9 | EXTI9, внешнее прерывание |
| CS | PF0 | выбор устройства |
| ADDR_OE | PF1 | разрешение адресного формирователя |
| DATA_OE | PF2 | разрешение DATA / Hi-Z |
| DATA_DIR | PF3 | направление DATA |
| AUX1 | PF4 | резерв |
| AUX2 | PF5 | резерв |

Встроенный RMII Ethernet не пересекается с этой разводкой.

## Обязательная доработка NUCLEO-F767ZI для PD8/PD9

Для использования полного GPIOD как ADDR[15:0]:

```text
SB5 = OPEN
SB6 = OPEN
SB7 = CLOSED
SB4 = CLOSED
```

## Как выполняется bus.write()

Lua:

```lua
bus.write(0x1234, 0x55AA)
```

BusTask делает аппаратный цикл:

```text
1. DATA_OE = disable
2. DATA_DIR = STM32 -> MODULE
3. GPIOE = OUTPUT
4. GPIOD->ODR = 0x1234
5. GPIOE->ODR = 0x55AA
6. ADDR_OE = enable
7. DATA_OE = enable
8. setup_us
9. CS = active
10. WR pulse
11. hold_us
12. CS = inactive
```

По умолчанию:

```text
setup = 1 us
WR pulse = 2 us
hold = 1 us
```

Задать времена из Lua:

```lua
bus.timing(2, 5, 3)
```

где аргументы — `setup_us`, `pulse_us`, `hold_us`.

## TIM3

TIM3 настроен на частоту 1 МГц, то есть один тик таймера = 1 мкс.

`WR` формируется функцией `pulseWrTimer()`:

```text
WR active
TIM3 one-shot на N микросекунд
WR inactive
```

FreeRTOS не задаёт длительность импульса. BusTask только запускает аппаратный микросекундный интервал и продолжает цикл после окончания таймера.

`STROBE` имеет аналогичную функцию `pulseStrobeTimer()` и готов для подключения к высокоуровневому API циклов.

## READY через EXTI

PC8 подключён к EXTI. При изменении READY ISR:

```text
READY edge
   -> EXTI callback
   -> setReadyFromISR()
   -> xTaskNotifyFromISR(BusTask)
```

Lua:

```lua
if bus.wait_ready(5000) then
    print("READY received")
else
    print("READY timeout")
end
```

`5000` — таймаут в микросекундах.

Для коротких ожиданий менее 1 мс используется DWT-дедлайн с асинхронным обновлением состояния от EXTI. Для более длинных ожиданий BusTask блокируется на task notification и не крутит CPU в пустом цикле.

## IRQ через EXTI

PC9 также работает через EXTI:

```text
IRQ edge
   -> EXTI callback
   -> setIrqFromISR()
   -> xTaskNotifyFromISR(BusTask)
```

Текущее состояние доступно из Lua:

```lua
if bus.irq() then
    print("IRQ active")
end
```

Позже на этой базе будет добавлен `bus.wait_irq()` и журнал событий IRQ.

## Инверсия

```lua
bus.invert_addr(true)
bus.invert_data(true)
bus.write(0x1234, 0x55AA)
```

Lua и журнал работают с логическими значениями, а инверсия применяется только на физической шине.

## Безопасное состояние

`BusEngine::emergencyStop()` устанавливает:

```text
WR      = inactive
STROBE  = inactive
CS      = inactive
DATA_OE = disable
ADDR_OE = disable
DATA    = input / Hi-Z
TIM3    = stopped
```

При `POST /api/stop` Lua получает запрос остановки, а шина немедленно переводится в безопасное состояние.

## Lua API

```lua
print("start")

bus.invert_addr(false)
bus.invert_data(false)
bus.timing(2, 5, 3)

bus.write(0x1000, 0x55AA)

if not bus.wait_ready(5000) then
    print("READY timeout")
    return
end

local value = bus.read(0x1000)
print(string.format("0x%04X", value))

if bus.irq() then
    print("IRQ")
end
```

## Следующие аппаратные этапы

1. DMA для быстрых массивов/циклов;
2. `bus.wait_irq()`;
3. пошаговый режим;
4. LOOP STEP для осциллографирования;
5. сохранение Lua-программ и профилей во flash;
6. веб-панель текущих ADDR/DATA/READY/IRQ и состояния цикла.

## Сборка

```bash
pio run
```

Прошивка:

```bash
pio run -t upload
```
