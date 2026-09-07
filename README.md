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
GPIO / DWT / later TIM+DMA
   |
3.3V <-> 5V level/bus transceivers
   |
Analyzed module
```

Веб, Lua и аппаратная шина работают независимо. HTTP не ждёт завершения Lua-программы.

## Принятая распиновка NUCLEO-F767ZI

Главный принцип: каждая 16-битная шина занимает полный GPIO-порт. Поэтому выставление 16 линий выполняется одной записью регистра.

### Шина адреса

| Сигнал | STM32 |
|---|---|
| ADDR0 | PD0 |
| ADDR1 | PD1 |
| ADDR2 | PD2 |
| ADDR3 | PD3 |
| ADDR4 | PD4 |
| ADDR5 | PD5 |
| ADDR6 | PD6 |
| ADDR7 | PD7 |
| ADDR8 | PD8 |
| ADDR9 | PD9 |
| ADDR10 | PD10 |
| ADDR11 | PD11 |
| ADDR12 | PD12 |
| ADDR13 | PD13 |
| ADDR14 | PD14 |
| ADDR15 | PD15 |

Физическая выдача слова:

```cpp
GPIOD->ODR = address;
```

### Шина данных

| Сигнал | STM32 |
|---|---|
| DATA0 | PE0 |
| DATA1 | PE1 |
| DATA2 | PE2 |
| DATA3 | PE3 |
| DATA4 | PE4 |
| DATA5 | PE5 |
| DATA6 | PE6 |
| DATA7 | PE7 |
| DATA8 | PE8 |
| DATA9 | PE9 |
| DATA10 | PE10 |
| DATA11 | PE11 |
| DATA12 | PE12 |
| DATA13 | PE13 |
| DATA14 | PE14 |
| DATA15 | PE15 |

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
| WR | PC6 | импульс записи; вывод поддерживает TIM3_CH1/TIM8_CH1 |
| STROBE | PC7 | дополнительный аппаратный строб |
| READY | PC8 | вход готовности, в дальнейшем EXTI8 |
| IRQ | PC9 | вход прерывания, в дальнейшем EXTI9 |
| CS | PF0 | выбор анализируемого устройства |
| ADDR_OE | PF1 | разрешение формирователя адресной шины |
| DATA_OE | PF2 | разрешение формирователя DATA / Hi-Z |
| DATA_DIR | PF3 | направление DATA |
| AUX1 | PF4 | резервный управляемый сигнал |
| AUX2 | PF5 | резервный управляемый сигнал |

Встроенный RMII Ethernet NUCLEO-F767ZI использует PA1, PA2, PA7, PB13, PC1, PC4, PC5, PG11 и PG13. Выбранная разводка с Ethernet не пересекается.

## Обязательная доработка NUCLEO-F767ZI для PD8/PD9

PD8 и PD9 по умолчанию подключены к ST-LINK Virtual COM Port. Для использования полного GPIOD как ADDR[15:0]:

```text
SB5 = OPEN
SB6 = OPEN

SB7 = CLOSED
SB4 = CLOSED
```

SB5/SB6 отключают USART ST-LINK от PD8/PD9. SB7/SB4 оставляют PD8/PD9 выведенными на Morpho-разъёмы.

После этого ST-LINK по-прежнему можно использовать для прошивки/отладки по SWD, но штатный VCP через PD8/PD9 использовать нельзя.

## Как выполняется bus.write()

Lua:

```lua
bus.write(0x1234, 0x55AA)
```

передаёт два 16-битных значения в BusTask. BusTask делает аппаратный цикл:

```text
1. DATA_OE = disable        // исключить конфликт драйверов
2. DATA_DIR = STM32 -> MODULE
3. GPIOE = OUTPUT
4. GPIOD->ODR = 0x1234      // сразу 16 адресных линий
5. GPIOE->ODR = 0x55AA      // сразу 16 линий данных
6. ADDR_OE = enable
7. DATA_OE = enable
8. ожидание setup_us
9. CS = active
10. WR = active
11. ожидание pulse_us
12. WR = inactive
13. ожидание hold_us
14. CS = inactive
```

Текущие значения времени по умолчанию:

```text
setup = 1 us
WR pulse = 2 us
hold = 1 us
```

Для первого аппаратного этапа микросекунды отсчитываются через DWT CYCCNT, а не через FreeRTOS tick. Следующий этап — перенести формирование критических стробов на TIM/DMA.

## Инверсия

Lua всегда оперирует логическими значениями:

```lua
bus.invert_addr(true)
bus.invert_data(true)
bus.write(0x1234, 0x55AA)
```

При включённой инверсии физически на внешний модуль выдаётся `~address` и/или `~data`, но программа и журнал работают с исходными значениями.

## Безопасное состояние

`BusEngine::emergencyStop()` устанавливает:

```text
WR      = inactive
STROBE  = inactive
CS      = inactive
DATA_OE = disable
ADDR_OE = disable
DATA    = input / Hi-Z со стороны STM32
```

Это состояние также устанавливается при инициализации BusEngine.

## Web UI

В браузере открывается IP платы. Интерфейс позволяет писать Lua-код, запускать его, останавливать и видеть журнал во время выполнения.

HTTP API:

- `GET /` — редактор;
- `GET /api/status` — состояние платы;
- `GET /api/output` — текущий вывод Lua;
- `POST /api/run` — поставить Lua-код в очередь выполнения;
- `POST /api/stop` — остановить Lua.

HTML хранится как обычный файл `data/index.html`. `scripts/embed_web.py` автоматически превращает его в C/C++ ресурс перед сборкой.

## Сеть

Сначала используется DHCP. Если DHCP не отвечает, применяется:

```text
IP:      192.168.1.77
Mask:    255.255.255.0
Gateway: 192.168.1.1
```

## Lua API

```lua
print("hello")
delay_us(10)

bus.invert_addr(false)
bus.invert_data(false)
bus.write(0x1000, 0x55AA)

local value = bus.read(0x1000)
print(string.format("0x%04X", value))

if bus.ready() then
    print("READY")
end

if bus.irq() then
    print("IRQ")
end
```

`bus.write()` уже управляет реальными GPIO GPIOD/GPIOE и управляющими линиями. `bus.read()` уже умеет переключать DATA в input и считывать GPIOE->IDR, но точный протокол чтения будет адаптирован под строб чтения анализируемого модуля.

## Следующие аппаратные этапы

1. READY через EXTI + task notification с timeout;
2. IRQ через EXTI + очередь событий;
3. WR/STROBE через аппаратный TIM;
4. DMA для быстрых массивов/циклов;
5. пошаговый режим;
6. LOOP STEP для осциллографирования;
7. сохранение Lua-программ и профилей во flash;
8. веб-панель текущих ADDR/DATA/READY/IRQ и состояния цикла.

## Сборка

```bash
pio run
```

Прошивка:

```bash
pio run -t upload
```
