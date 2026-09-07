# OSTester

Анализатор/формирователь 16-разрядной шины для NUCLEO-F767ZI.

## Цель

Устройство подключается между STM32 и анализируемым модулем через управляемые шинные формирователи 3.3/5 В.

Поддерживаемые линии:

- DATA[15:0] — двунаправленная шина, 3-state;
- ADDR[15:0] — однонаправленная шина;
- WR — запись;
- CS — выбор кристалла;
- STROBE — строб;
- READY — готовность периферии;
- IRQ — прерывание от периферии;
- DATA_OE / DATA_DIR — управление формирователем данных;
- ADDR_OE — управление формирователем адреса.

## Архитектура

```text
Browser
   |
Ethernet / HTTP
   |
WebServerApp
   |
Lua ScriptEngine
   |
BusController (следующий этап)
   |
GPIO + level shifters
   |
Analyzed module
```

## Web UI

В браузере открывается IP платы. Интерфейс позволяет писать Lua-код, запускать его и видеть результат выполнения.

HTTP API:

- `GET /` — редактор;
- `GET /api/status` — состояние платы;
- `POST /api/run` — выполнить Lua-код из тела запроса;
- `POST /api/stop` — запрос остановки.

HTML хранится как обычный файл `data/index.html`. Скрипт `scripts/embed_web.py` автоматически превращает его в C/C++ ресурс перед сборкой. Поэтому отдельная файловая система на первом этапе не требуется.

## Сеть

Сначала используется DHCP. Если DHCP не отвечает, применяется:

```text
IP:      192.168.1.77
Mask:    255.255.255.0
Gateway: 192.168.1.1
```

IP также выводится в Serial 115200.

## Lua API — текущий каркас

```lua
print("hello")
delay_us(10)

bus.invert_addr(false)
bus.invert_data(false)
bus.write(0x1000, 0x55AA)
local value = bus.read(0x1000)
print(value)

if bus.ready() then
    print("READY")
end

if bus.irq() then
    print("IRQ")
end
```

На текущем этапе `bus.write/read/ready/irq` уже доступны из Lua, но аппаратный слой GPIO ещё не подключён: это следующий блок разработки.

## Следующий этап

1. `BusController` с прямой 16-битной записью портов STM32;
2. DATA input/output и управление DATA_DIR/DATA_OE;
3. ADDR_OE;
4. WR/CS/STROBE;
5. инверсия DATA/ADDR только на физическом выходе;
6. READY с timeout;
7. IRQ через EXTI;
8. точные интервалы через DWT CYCCNT;
9. пошаговое выполнение Lua/цикла;
10. loop одного шага для осциллографирования;
11. сохранение/загрузка Lua-программ во flash.

## Сборка

```bash
pio run
```

Прошивка:

```bash
pio run -t upload
```

Монитор порта:

```bash
pio device monitor
```
