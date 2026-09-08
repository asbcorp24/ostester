# OSTester

Анализатор/формирователь 16-разрядной параллельной шины на NUCLEO-F767ZI с Ethernet, веб-интерфейсом, FreeRTOS и встроенным Lua.

## Что делает устройство

OSTester подключается к анализируемому модулю через шинные формирователи 3.3/5 В и позволяет из браузера:

- писать и запускать Lua-сценарии;
- формировать 16-битный адрес и 16-битные данные;
- выполнять запись/чтение;
- управлять WR, CS, STROBE, OE и DIR;
- ждать READY;
- контролировать IRQ;
- задавать микросекундные тайминги;
- проверять прочитанные значения;
- сохранять Lua-скрипты во внутреннюю Flash STM32;
- открывать, перезаписывать и удалять сохранённые скрипты после перезагрузки.

## Архитектура

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

Хранилище Lua работает отдельно:

```text
Browser
   |
Save / Open / Delete
   |
HTTP API
   |
ScriptStore
   |
EEPROM emulation
   |
Internal STM32 Flash
```

## Распиновка NUCLEO-F767ZI

### ADDR[15:0]

```text
ADDR0  -> PD0
ADDR1  -> PD1
...
ADDR15 -> PD15
```

Все 16 линий адреса занимают полный GPIOD:

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

Запись:

```cpp
GPIOE->ODR = data;
```

Чтение:

```cpp
uint16_t data = (uint16_t)GPIOE->IDR;
```

### Управляющие линии

| Сигнал | STM32 | Назначение |
|---|---|---|
| WR | PC6 | импульс записи |
| STROBE | PC7 | дополнительный строб |
| READY | PC8 | EXTI8 |
| IRQ | PC9 | EXTI9 |
| CS | PF0 | выбор устройства |
| ADDR_OE | PF1 | OE адресного формирователя |
| DATA_OE | PF2 | OE DATA / Hi-Z |
| DATA_DIR | PF3 | направление DATA |
| AUX1 | PF4 | резерв |
| AUX2 | PF5 | резерв |

Встроенный RMII Ethernet не пересекается с этой разводкой.

## Важно: PD8/PD9 и ST-LINK VCP

Для полного GPIOD:

```text
SB5 = OPEN
SB6 = OPEN
SB7 = CLOSED
SB4 = CLOSED
```

После этого PD8/PD9 доступны на Morpho. ST-LINK по SWD продолжает работать, но штатный Virtual COM через PD8/PD9 использовать нельзя.

## Ethernet

Сначала используется DHCP. Если DHCP не отвечает, применяется:

```text
IP:      192.168.1.77
Mask:    255.255.255.0
Gateway: 192.168.1.1
```

Для прямого подключения к ПК можно задать компьютеру:

```text
IP:   192.168.1.10
Mask: 255.255.255.0
```

После прошивки открыть:

```text
http://192.168.1.77/
```

Если DHCP выдал другой адрес, использовать адрес, полученный от роутера.

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
setup    = 1 us
WR pulse = 2 us
hold     = 1 us
```

Из Lua:

```lua
bus.timing(2, 5, 3)
```

## TIM3

TIM3 используется как микросекундная временная база:

```text
1 tick = 1 us
```

Критические импульсы не формируются через `vTaskDelay()` FreeRTOS.

## READY / IRQ

READY:

```text
PC8 -> EXTI8 -> ISR -> xTaskNotifyFromISR(BusTask)
```

Lua:

```lua
if bus.wait_ready(5000) then
    print("READY")
else
    print("READY timeout")
end
```

IRQ:

```text
PC9 -> EXTI9 -> ISR -> BusTask notification
```

Lua:

```lua
if bus.irq() then
    print("IRQ active")
end
```

## Инверсия

```lua
bus.invert_addr(true)
bus.invert_data(true)
```

Lua работает с логическими значениями. Инверсия применяется на физической шине.

# Lua API

## bus.write(address, data)

```lua
local ok = bus.write(0x1000, 0x55AA)
```

Возвращает `true/false`.

## bus.read(address)

```lua
local value, err = bus.read(0x1000)
```

При успехе возвращает DATA, при ошибке `nil, error`.

## bus.write_ex(address, data)

```lua
local r = bus.write_ex(0x1000, 0x55AA)

print(r.ok)
print(r.address)
print(r.data)
print(r.ready)
print(r.irq)
print(r.time_us)
print(r.error)
```

Формат:

```lua
{
    ok = true,
    address = 0x1000,
    data = 0x55AA,
    ready = false,
    irq = false,
    time_us = 8,
    error = nil
}
```

## bus.read_ex(address)

```lua
local r = bus.read_ex(0x1000)

if r.ok then
    print(string.format("DATA=%04X", r.data))
else
    print(r.error)
end
```

Путь результата:

```text
Module -> DATA[15:0] -> GPIOE->IDR -> BusTask -> Lua -> print() -> Web
```

## bus.expect(address, expected [, mask])

```lua
local r = bus.expect(0x1000, 0x55AA)
```

Возвращает в том числе:

```lua
r.ok
r.matched
r.address
r.data
r.expected
r.mask
r.time_us
r.error
```

Проверка только младшего байта:

```lua
local r = bus.expect(0x2000, 0x005A, 0x00FF)
```

## Остальные функции

```lua
bus.wait_ready(timeout_us)
bus.ready()
bus.irq()
bus.timing(setup_us, pulse_us, hold_us)
bus.invert_addr(enabled)
bus.invert_data(enabled)
delay_us(us)
```

# Постоянное хранение Lua во Flash

Lua-скрипты теперь можно сохранять прямо из веб-интерфейса. Используется `ScriptStore`, который работает через EEPROM-emulation STM32 core во внутренней Flash микроконтроллера.

Файлы проекта:

```text
include/ScriptStore.h
src/ScriptStore.cpp
```

## Лимиты текущей версии

```text
Количество слотов:       6
Максимум имени:          31 символ
Максимум Lua-скрипта:    1900 байт
```

Разрешённые символы имени:

```text
A-Z a-z 0-9 _ - .
```

Примеры:

```text
memory_test.lua
clear_bus.lua
module_1.lua
osc_wr.lua
```

## Что хранится в каждом слоте

```text
magic
length
checksum
name
Lua source code
```

Для кода рассчитывается контрольная сумма. Повреждённая запись не будет выдана как корректный скрипт.

После выключения питания или reset сохранённые скрипты остаются во Flash.

## Веб-интерфейс хранения

Над редактором Lua теперь есть:

```text
[список скриптов]
[имя скрипта]
[Новый]
[Открыть]
[Сохранить]
[Удалить]
```

Типичный порядок:

```text
1. написать Lua
2. указать имя memory_test.lua
3. нажать Сохранить
4. ScriptStore записывает код во Flash
5. после перезагрузки открыть список
6. выбрать memory_test.lua
7. нажать Открыть
8. нажать Запустить
```

При повторном сохранении с тем же именем слот перезаписывается.

## HTTP API скриптов

Получить список:

```text
GET /api/scripts
```

Ответ:

```json
{
  "ok": true,
  "maxScripts": 6,
  "maxScriptBytes": 1900,
  "scripts": [
    {
      "name": "memory_test.lua",
      "length": 412,
      "checksum": 123456789
    }
  ]
}
```

Открыть:

```text
GET /api/script?name=memory_test.lua
```

Сохранить:

```text
POST /api/script?name=memory_test.lua
Content-Type: text/plain

<Lua source>
```

Удалить:

```text
DELETE /api/script?name=memory_test.lua
```

## Важное замечание по ресурсу Flash

Внутренняя Flash имеет ограниченный ресурс циклов erase/write. Скрипты рассчитаны на обычное пользовательское сохранение, а не на запись сотни раз в секунду. Для частых логов или больших объёмов позже лучше использовать внешнюю память.

# Пример теста памяти

```lua
bus.invert_addr(false)
bus.invert_data(false)
bus.timing(2, 5, 3)

local errors = 0

for addr = 0x0000, 0x00FE, 2 do
    local expected = 0x55AA

    local w = bus.write_ex(addr, expected)
    if not w.ok then
        print(string.format("WRITE ERROR %04X", addr))
        errors = errors + 1
    else
        local r = bus.expect(addr, expected)
        if not r.ok then
            print(string.format(
                "DEFECT addr=%04X expected=%04X actual=%04X",
                addr,
                expected,
                r.data
            ))
            errors = errors + 1
        end
    end
end

print("Total errors:", errors)
```

# Журнал

Lua `print()` и диагностические сообщения BusEngine доступны через:

```text
GET /api/output
```

Пример:

```text
[BUS] WRITE_EX addr=0x1000 data=0x55AA OK time=8 us READY=0 IRQ=0
[BUS] READ_EX addr=0x1000 -> 0x55AA OK time=7 us READY=1 IRQ=0
[BUS] EXPECT addr=0x1000 expected=0x55AA actual=0x55AA mask=0xFFFF OK
```

# Безопасный STOP

`POST /api/stop` вызывает остановку Lua и `BusEngine::emergencyStop()`.

Безопасное состояние:

```text
WR      = inactive
STROBE  = inactive
CS      = inactive
DATA_OE = disable
ADDR_OE = disable
DATA    = input / Hi-Z
TIM3    = stopped
```

# Основной HTTP API

```text
GET    /                 web editor
GET    /api/status       состояние платы
GET    /api/output       Lua/log output
POST   /api/run          выполнить текущий Lua
POST   /api/stop         остановить Lua и шину
GET    /api/scripts      список сохранённых Lua
GET    /api/script       открыть Lua
POST   /api/script       сохранить Lua
DELETE /api/script       удалить Lua
```

# Сборка

```bash
pio run -t clean
pio run
```

Прошивка через встроенный ST-LINK:

```bash
pio run -t upload
```

`platformio.ini` использует:

```ini
upload_protocol = stlink
```

## Следующие этапы

1. DMA для быстрых массивов/циклов;
2. `bus.wait_irq()`;
3. пошаговый режим STEP;
4. LOOP STEP для осциллографа;
5. веб-панель ADDR/DATA/READY/IRQ;
6. статистика ошибок и отчёты теста;
7. при необходимости расширение хранения на внешнюю Flash/SD.
