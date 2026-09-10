# Terminal (универсальный терминал) — сквозной тест с ПДА

> Прошивка: `proshivki/Terminal/Terminal_ESP32/Terminal_ESP32.ino` v1.0  
> Карта каналов: `proshivki/common/mux_channels.h`  
> ПДА опрашивает **CH0** (`MUX_CH_UNIVERSAL`) каждые ~200 ms.

## Роли терминала

| Роль | Назначение | op по умолчанию | Serial |
|------|------------|-----------------|--------|
| `STORE` | Касса (покупка) | 0 PURCHASE | `TERMINAL_ROLE:STORE` |
| `ATM` | Банкомат | 4 ATM | `TERMINAL_ROLE:ATM` |
| `QUEST` | Квестовая доска | 2 QUEST | `TERMINAL_ROLE:QUEST` |
| `ADMIT` | Допуск в игру | 3 ADMIT | `TERMINAL_ROLE:ADMIT` |
| `BANK` | Банк | 1 BANK | `TERMINAL_ROLE:BANK` |

Роль хранится в NVS (`terminal_role`) и переживает перезагрузку ESP32.

## Компиляция в Arduino IDE

**Открывайте скетч только из репозитория:**

`proshivki/Terminal/Terminal_ESP32/Terminal_ESP32.ino`

| Параметр | Значение |
|----------|----------|
| Board | **ESP32 Dev Module** (WROOM-32) |
| Monitor | **115200** бод |

Стенд с TCA на ПДА: `#define USE_TCA_MUX 1` (по умолчанию).

---

## Пошаговый тест (роль STORE — касса)

### 1. Serial Monitor (115200)

```
STALKER_WHO
TERMINAL_ROLE
PING
EEPROM_PING
TXN_RESET
```

Ожидание: `STALKER:TERMINAL:v1,role=STORE,...`, `TERMINAL_ROLE:STORE,...`, `PONG`, `EEPROM:OK`, `OK:TXN_IDLE`.

### 2. Покупка (без явного op — используется роль STORE)

```
TXN_START:amount=100,item=1
TXN_WAIT:timeout_ms=30000
```

### 3. Смена роли на банкомат

```
TERMINAL_ROLE:ATM
TXN_START:amount=500,item=0
```

`op` не указан → автоматически `op=4` (ATM).

### 4. Допуск (роль ADMIT)

```
TERMINAL_ROLE:ADMIT
TXN_RESET
TXN_START
TXN_WAIT:timeout_ms=30000
```

Для ADMIT поля `amount`/`item` не нужны.

---

## Программатор PC

1. Запустить `programmat_pc/programmer.py`.
2. Подключить ESP32 с `Terminal_ESP32` — автоопределение `TERMINAL`.
3. Строка **РОЛЬ** ◀ ▶ — выбор STORE / ATM / QUEST / ADMIT / BANK (команда `TERMINAL_ROLE:...`).
4. **TXN СТАРТ** / **ЖДАТЬ PDA** — как у кассы.

---

## Связанные файлы

- `proshivki/Terminal/Terminal_ESP32/` — прошивка  
- `proshivki/Cashier/README_MOVED.md` — старый путь (deprecated)  
- `test_firmware/EEPROM_TCA_Test/README_EEPROM_TCA_Test.md` — TCA/EEPROM на CH0
