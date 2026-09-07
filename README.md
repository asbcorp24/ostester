# OSTester

Анализатор/формирователь 16-разрядной шины для NUCLEO-F767ZI.

## Назначение

Устройство подключается между STM32 и анализируемым модулем через управляемые шинные формирователи 3.3/5 В и позволяет из веб-интерфейса писать Lua-сценарии для формирования адресов, данных, циклов записи/чтения и проверки ответов периферии.

Поддерживаемые линии:

- DATA[15:0] — двунаправленная 16-битная шина, 3-state;
- ADDR[15:0] — однонаправленная 16-битная шина;
- WR — запись;
- CS — выбор устройства;
- STROBE — дополнительный строб;
- READY — готовность периферии;
- IRQ — внешнее прерывание;
- DATA_OE / DATA_DIR — управление формирователем DATA;
- ADDR_OE — управление формирователем ADDR.

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
3.3V <-> 5V bus transceivers
   |
Analyzed module
```

Веб, Lua и аппаратная шина работают независимо. HTTP не ждёт завершения Lua-программы.

## Распиновка NUCLEO-F767ZI

### ADDR[15:0]

```text
ADDR0  -> PD0
ADDR1  -> PD1
...
ADDR15 -> PD15
```

Все 16 адресных линий занимают полный GPIOD, поэтому слово выставляется одной операцией:

```cpp
GPIOD->ODR = address;
```

### DATA[15:0]

```text
DATA0  -> PE0
DATA1  -> PE1
...
DATA15 -> PE15
```

Выдача 16-битного слова:

```cpp
GPIOE->ODR = data;
```

Чтение 16-битного слова:

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

## Важно: PD8/PD9 и ST-LINK VCP

Чтобы использовать полный GPIOD как ADDR[15:0]:

```text
SB5 = OPEN
SB6 = OPEN
SB7 = CLOSED
SB4 = CLOSED
```

После этого PD8/PD9 доступны на Morpho, но штатный ST-LINK Virtual COM Port через эти линии использовать нельзя. Прошивка и отладка по SWD остаются доступны.

## Как выполняется bus.write()

Lua:

```lua
bus.write(0x1234, 0x55AA)
```

BusTask выполняет:

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

Задать времена:

```lua
bus.timing(2, 5, 3)
```

где аргументы — `setup_us`, `pulse_us`, `hold_us`.

## TIM3

TIM3 работает с частотой 1 МГц:

```text
1 tick = 1 us
```

WR формируется аппаратным микросекундным интервалом. FreeRTOS не задаёт длительность импульса.

## READY через EXTI

READY подключён к PC8 / EXTI8.

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

## IRQ через EXTI

IRQ подключён к PC9 / EXTI9.

```lua
if bus.irq() then
    print("IRQ active")
end
```

## Инверсия

```lua
bus.invert_addr(true)
bus.invert_data(true)
bus.write(0x1234, 0x55AA)
```

Lua всегда работает с логическими значениями. Инверсия применяется только при физической выдаче/чтении шины.

---

# Lua API

## 1. bus.write(address, data)

Простая запись. Возвращает `true` или `false`.

```lua
local ok = bus.write(0x1000, 0x55AA)

if ok then
    print("write OK")
else
    print("write timeout")
end
```

## 2. bus.read(address)

Простое чтение.

```lua
local value, err = bus.read(0x1000)

if value then
    print(string.format("DATA = 0x%04X", value))
else
    print("ERROR:", err)
end
```

При успехе возвращается значение DATA. При ошибке:

```text
nil, "bus read timeout"
```

## 3. bus.write_ex(address, data)

Расширенная запись. Возвращает Lua-таблицу с результатом операции.

```lua
local r = bus.write_ex(0x1000, 0x55AA)

print("ok       =", r.ok)
print(string.format("address  = 0x%04X", r.address))
print(string.format("data     = 0x%04X", r.data))
print("ready    =", r.ready)
print("irq      =", r.irq)
print("time_us  =", r.time_us)
print("error    =", r.error)
```

Формат результата:

```lua
{
    ok = true,
    address = 0x1000,
    data = 0x55AA,
    ready = false,
    irq = false,
    time_us = 9,
    error = nil
}
```

Если операция завершилась ошибкой:

```lua
{
    ok = false,
    address = 0x1000,
    data = 0x55AA,
    ready = false,
    irq = false,
    time_us = 1000000,
    error = "bus write timeout"
}
```

`time_us` — время выполнения операции от вызова Lua до возврата результата.

## 4. bus.read_ex(address)

Расширенное чтение.

```lua
local r = bus.read_ex(0x1000)

if r.ok then
    print(string.format(
        "ADDR=%04X DATA=%04X time=%u us",
        r.address,
        r.data,
        r.time_us
    ))
else
    print("READ ERROR:", r.error)
end
```

Результат:

```lua
{
    ok = true,
    address = 0x1000,
    data = 0xA55A,
    ready = true,
    irq = false,
    time_us = 7,
    error = nil
}
```

Путь данных:

```text
Analyzed module
      |
DATA[15:0]
      |
GPIOE->IDR
      |
BusTask
      |
Lua r.data
      |
print()
      |
WebTask
      |
Browser
```

## 5. bus.expect(address, expected [, mask])

Читает значение по адресу и сразу сравнивает его с ожидаемым.

Простая проверка полного 16-битного слова:

```lua
local r = bus.expect(0x1000, 0x55AA)

if r.ok then
    print("TEST OK")
else
    print(string.format(
        "FAIL addr=%04X expected=%04X actual=%04X error=%s",
        r.address,
        r.expected,
        r.data,
        tostring(r.error)
    ))
end
```

Результат успешной проверки:

```lua
{
    ok = true,
    matched = true,
    address = 0x1000,
    data = 0x55AA,
    expected = 0x55AA,
    mask = 0xFFFF,
    ready = true,
    irq = false,
    time_us = 8,
    error = nil
}
```

Если значение отличается:

```lua
{
    ok = false,
    matched = false,
    address = 0x1000,
    data = 0x55AB,
    expected = 0x55AA,
    mask = 0xFFFF,
    error = "value mismatch"
}
```

### Маска сравнения

Третий аргумент позволяет проверять только выбранные биты.

Например, проверить только младший байт:

```lua
local r = bus.expect(0x2000, 0x005A, 0x00FF)
```

Сравнивается:

```text
actual   & 0x00FF
expected & 0x00FF
```

Это удобно для статусных регистров, где часть битов может меняться независимо.

## 6. bus.wait_ready(timeout_us)

```lua
local ready = bus.wait_ready(5000)
```

Возвращает:

```text
true  - READY появился до таймаута
false - таймаут
```

## 7. bus.ready()

Текущее состояние READY:

```lua
if bus.ready() then
    print("READY=1")
end
```

## 8. bus.irq()

Текущее состояние IRQ:

```lua
if bus.irq() then
    print("IRQ=1")
end
```

## 9. bus.timing(setup_us, pulse_us, hold_us)

```lua
bus.timing(2, 5, 3)
```

## 10. bus.invert_addr(enabled)

```lua
bus.invert_addr(true)
```

## 11. bus.invert_data(enabled)

```lua
bus.invert_data(true)
```

---

# Примеры тестов

## Проверка одной ячейки

```lua
bus.timing(2, 5, 3)

local w = bus.write_ex(0x1000, 0x55AA)
if not w.ok then
    print("WRITE ERROR:", w.error)
    return
end

local r = bus.expect(0x1000, 0x55AA)

if r.ok then
    print("CELL OK")
else
    print(string.format(
        "CELL FAIL: addr=%04X expected=%04X actual=%04X",
        r.address,
        r.expected,
        r.data
    ))
end
```

## Проверка массива адресов

```lua
local errors = 0

for addr = 0x0000, 0x00FE, 2 do
    local expected = 0x55AA

    local w = bus.write_ex(addr, expected)
    if not w.ok then
        print(string.format("WRITE TIMEOUT %04X", addr))
        errors = errors + 1
    else
        local r = bus.expect(addr, expected)

        if not r.ok then
            print(string.format(
                "MISMATCH addr=%04X expected=%04X actual=%04X",
                addr,
                expected,
                r.data
            ))
            errors = errors + 1
        end
    end
end

print("Errors:", errors)
```

## Тест шаблона 0xAAAA / 0x5555

```lua
local patterns = {0xAAAA, 0x5555}
local errors = 0

for _, pattern in ipairs(patterns) do
    for addr = 0x0000, 0x00FE, 2 do
        if not bus.write(addr, pattern) then
            errors = errors + 1
        end
    end

    for addr = 0x0000, 0x00FE, 2 do
        local r = bus.expect(addr, pattern)
        if not r.ok then
            errors = errors + 1
            print(string.format(
                "FAIL pattern=%04X addr=%04X got=%04X",
                pattern,
                addr,
                r.data
            ))
        end
    end
end

print("Total errors:", errors)
```

## Проверка READY после записи

```lua
local w = bus.write_ex(0x1000, 0x1234)

if not w.ok then
    print("write failed")
    return
end

if not bus.wait_ready(5000) then
    print("Peripheral READY timeout")
    return
end

local r = bus.read_ex(0x1000)
print(string.format("result = %04X", r.data))
```

---

# Журнал в веб-интерфейсе

Расширенные операции автоматически пишут диагностические строки.

Пример:

```text
[BUS] WRITE_EX addr=0x1000 data=0x55AA OK time=8 us READY=0 IRQ=0
[BUS] READ_EX addr=0x1000 -> 0x55AA OK time=7 us READY=1 IRQ=0
[BUS] EXPECT addr=0x1000 expected=0x55AA actual=0x55AA mask=0xFFFF OK time=7 us
```

Lua `print()` попадает в тот же журнал, который браузер получает через:

```text
GET /api/output
```

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

## HTTP API

```text
GET  /             web editor
GET  /api/status   состояние
GET  /api/output   текущий Lua/log output
POST /api/run      поставить Lua-код в очередь
POST /api/stop     остановить Lua и шину
```

## Следующие этапы

1. DMA для быстрых массивов и циклов;
2. `bus.wait_irq()`;
3. пошаговый режим;
4. LOOP STEP для осциллографирования;
5. сохранение Lua-программ и профилей во flash;
6. веб-панель текущих ADDR/DATA/READY/IRQ;
7. статистика ошибок и отчёт теста.

## Сборка

```bash
pio run
```

Прошивка:

```bash
pio run -t upload
```
