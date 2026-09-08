# OSTester

Анализатор/формирователь 16-разрядной параллельной шины на NUCLEO-F767ZI с Ethernet, FreeRTOS, встроенным Lua, OLED 128x64, энкодером и постоянным хранением настроек/скриптов во Flash.

## Возможности

- DATA[15:0] — двунаправленная 16-битная шина;
- ADDR[15:0] — 16-битная адресная шина;
- WR, CS, STROBE, READY, IRQ, DATA_OE, DATA_DIR, ADDR_OE;
- прямые 16-битные операции через GPIOD/GPIOE;
- точные микросекундные интервалы TIM3/DWT;
- READY/IRQ через EXTI;
- Lua из браузера;
- `bus.write/read/write_ex/read_ex/expect`;
- сохранение Lua во внутреннюю Flash;
- Ethernet HTTP интерфейс;
- OLED 128x64 для просмотра текущего IP;
- энкодер с кнопкой для настройки DHCP/IP/MASK/GATEWAY/DNS;
- сохранение сетевых настроек во Flash;
- отдельные FreeRTOS задачи Web/Lua/Bus/UI.

# Архитектура

```text
                         +--------------------+
Browser <--- Ethernet -->| WebTask priority 3 |
                         +---------+----------+
                                   |
                         +---------v----------+
                         | LuaTask priority 4 |
                         +---------+----------+
                                   |
                              Bus Queue
                                   |
                         +---------v----------+
                         | BusTask priority 5 |
                         +---------+----------+
                                   |
                         GPIO / TIM3 / EXTI
                                   |
                              test module

OLED + Encoder
      |
+-----v-------------+
| UiTask priority 2 |
+-------------------+
      |
NetworkSettings -> EEPROM emulation -> STM32 Flash
```

# Распиновка шин

## ADDR[15:0]

```text
ADDR0  -> PD0
ADDR1  -> PD1
...
ADDR15 -> PD15
```

Выдача слова:

```cpp
GPIOD->ODR = address;
```

## DATA[15:0]

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

## Управляющие линии

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

## PD8/PD9 и ST-LINK VCP

Для использования полного GPIOD:

```text
SB5 = OPEN
SB6 = OPEN
SB7 = CLOSED
SB4 = CLOSED
```

SWD прошивка/отладка остаётся доступной. Штатный ST-LINK Virtual COM через PD8/PD9 после этого не используется.

# OLED 128x64

Текущая реализация рассчитана на стандартный I2C OLED 128x64 с контроллером SSD1306, обычно адрес `0x3C`.

Используется библиотека:

```ini
olikraus/U8g2
```

Подключение:

| OLED | NUCLEO-F767ZI |
|---|---|
| VCC | 3.3V |
| GND | GND |
| SCL | PB8 |
| SDA | PB9 |

I2C:

```text
PB8 = SCL
PB9 = SDA
```

Если конкретный модуль имеет SH1106 вместо SSD1306, нужно заменить U8g2-конструктор в `LocalUi`, остальная логика меню остаётся прежней.

# Энкодер

Используется библиотека:

```ini
paulstoffregen/Encoder
```

Подключение:

| Энкодер | NUCLEO-F767ZI |
|---|---|
| CLK / A | PG6 |
| DT / B | PG7 |
| SW | PG8 |
| + | 3.3V |
| GND | GND |

Кнопка SW используется с внутренней подтяжкой `INPUT_PULLUP`.

Пины PB8/PB9 и PG6/PG7/PG8 не пересекаются с принятыми линиями ADDR/DATA и RMII Ethernet.

# Что показывает дисплей

Главный экран:

```text
OSTester
---------------------
IP: 192.168.1.77
Mode: DHCP
Link: UP
                 MENU
```

IP на экране — именно текущий адрес `Ethernet.localIP()`, то есть при DHCP будет показан адрес, реально выданный роутером.

Нажатие энкодера открывает меню:

```text
NETWORK SETTINGS
---------------------
>DHCP: ON
 IP
 MASK
 GATEWAY
```

Пункты меню:

```text
DHCP
IP
MASK
GATEWAY
DNS
SAVE+REBOOT
BACK
```

## Управление

```text
вращение         -> выбор пункта / изменение числа
короткое нажатие -> вход / подтверждение
```

При редактировании IP каждый октет меняется отдельно:

```text
EDIT IP
---------------------
192.168.1.77
Octet 1 = 192
Rotate / press=next
```

После нажатия переход к следующему октету.

После четвёртого октета происходит возврат в меню.

# Сетевые режимы

## DHCP

Если:

```text
DHCP = ON
```

при запуске выполняется DHCP.

Если DHCP успешно ответил, текущий адрес выводится на OLED.

Если DHCP не ответил, используется сохранённая статическая конфигурация как fallback.

Начальные значения:

```text
IP:      192.168.1.77
MASK:    255.255.255.0
GATEWAY: 192.168.1.1
DNS:     192.168.1.1
```

## STATIC

Если:

```text
DHCP = OFF
```

Ethernet сразу запускается с сохранёнными:

```text
IP
MASK
GATEWAY
DNS
```

Пример прямого подключения к ПК:

```text
NUCLEO:
IP   192.168.1.77
MASK 255.255.255.0

PC:
IP   192.168.1.10
MASK 255.255.255.0
```

После этого открыть:

```text
http://192.168.1.77/
```

# Сохранение настроек

Сетевые настройки хранятся во внутренней Flash через EEPROM emulation.

Файлы:

```text
include/NetworkSettings.h
src/NetworkSettings.cpp
```

Сохраняются:

```text
DHCP
IP
MASK
GATEWAY
DNS
checksum
```

В меню выбрать:

```text
SAVE+REBOOT
```

После этого:

```text
1. настройки записываются во Flash
2. OLED показывает Settings saved
3. выполняется NVIC_SystemReset()
4. Ethernet запускается с новой конфигурацией
5. OLED показывает новый текущий IP
```

Первые 256 байт EEPROM-emulation зарезервированы под настройки устройства.

Lua ScriptStore начинается после этого диапазона, поэтому настройки сети и Lua-скрипты не перекрываются.

Из-за изменения разметки текущая версия ScriptStore имеет `STORE_VERSION = 2`.

# FreeRTOS задачи

```text
BusTask priority 5
LuaTask priority 4
WebTask priority 3
UiTask  priority 2
```

`UiTask` опрашивает энкодер примерно каждые 5 мс и обновляет OLED независимо от BusTask.

Точные сигналы шины не формируются UiTask или WebTask.

# Ethernet

Используются:

```ini
stm32duino/STM32duino LwIP
stm32duino/STM32Ethernet
```

В WebTask вызывается:

```cpp
Ethernet.schedule();
```

для обслуживания STM32 Ethernet/LwIP стека.

# Выполнение bus.write()

Lua:

```lua
bus.write(0x1234, 0x55AA)
```

Последовательность:

```text
DATA_OE disable
DATA_DIR STM32 -> MODULE
GPIOE output
GPIOD->ODR = 0x1234
GPIOE->ODR = 0x55AA
ADDR_OE enable
DATA_OE enable
setup_us
CS active
WR pulse
hold_us
CS inactive
```

По умолчанию:

```text
setup    = 1 us
WR pulse = 2 us
hold     = 1 us
```

Настройка Lua:

```lua
bus.timing(2, 5, 3)
```

# READY / IRQ

READY:

```text
PC8 -> EXTI8 -> ISR -> BusTask notification
```

```lua
if bus.wait_ready(5000) then
    print("READY")
else
    print("READY timeout")
end
```

IRQ:

```text
PC9 -> EXTI9
```

```lua
if bus.irq() then
    print("IRQ active")
end
```

# Lua API

Основные функции:

```lua
bus.write(address, data)
bus.read(address)
bus.write_ex(address, data)
bus.read_ex(address)
bus.expect(address, expected [, mask])
bus.wait_ready(timeout_us)
bus.ready()
bus.irq()
bus.timing(setup_us, pulse_us, hold_us)
bus.invert_addr(enabled)
bus.invert_data(enabled)
delay_us(us)
```

Расширенный результат:

```lua
local r = bus.read_ex(0x1000)

print(r.ok)
print(r.address)
print(r.data)
print(r.ready)
print(r.irq)
print(r.time_us)
print(r.error)
```

Проверка значения:

```lua
local r = bus.expect(0x1000, 0x55AA)

if not r.ok then
    print(string.format(
        "DEFECT addr=%04X expected=%04X actual=%04X",
        r.address,
        r.expected,
        r.data
    ))
end
```

# Lua-скрипты во Flash

Через веб-интерфейс доступны:

```text
Новый
Открыть
Сохранить
Удалить
```

Текущие лимиты:

```text
6 скриптов
до 1900 байт на скрипт
имя до 31 символа
```

HTTP API:

```text
GET    /api/scripts
GET    /api/script?name=test.lua
POST   /api/script?name=test.lua
DELETE /api/script?name=test.lua
```

# HTTP API

```text
GET    /                 web editor
GET    /api/status       состояние/IP/DHCP
GET    /api/output       Lua/log output
POST   /api/run          выполнить Lua
POST   /api/stop         STOP
GET    /api/scripts      список Lua
GET    /api/script       открыть Lua
POST   /api/script       сохранить Lua
DELETE /api/script       удалить Lua
```

# Безопасный STOP

При STOP:

```text
WR      inactive
STROBE  inactive
CS      inactive
DATA_OE disable
ADDR_OE disable
DATA    input / Hi-Z
TIM3    stop
```

# PlatformIO зависимости

```ini
lib_deps =
    stm32duino/STM32duino LwIP
    stm32duino/STM32Ethernet
    stm32duino/STM32duino FreeRTOS
    olikraus/U8g2
    paulstoffregen/Encoder
    https://github.com/DECE2183/libLua.git
```

# Сборка

После обновления зависимостей рекомендуется чистая сборка:

```bash
pio run -t clean
pio run
```

Прошивка через встроенный ST-LINK:

```bash
pio run -t upload
```

# Следующие этапы

- DMA для быстрых циклов;
- `bus.wait_irq()`;
- STEP;
- LOOP STEP для осциллографа;
- веб-панель текущих ADDR/DATA/READY/IRQ;
- статистика ошибок и отчёт теста.
