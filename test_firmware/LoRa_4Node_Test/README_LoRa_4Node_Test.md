# LoRa — тест 4 узлов (ESP32-WROOM-32 + Ra-01)

> Спецификация протокола: `MASTER_SPECIFICATION.md` §3 (LoRa).  
> Старые 2-узловые скетчи: `LoRa_Test_Sender/`, `LoRa_Test_Receiver/`.

## Что делает прошивка

Каждый из **4 ESP32** одновременно **передаёт и принимает** LoRa-пакеты и в Serial Monitor показывает:

| Вопрос | Как видно в логе |
|--------|------------------|
| **Кто передал?** | `ОТ КОГО: ESP-2 (id=2)` — поле `src_id` |
| **Общий сигнал или личный?** | `BROADCAST (для всех)` или `UNICAST (мне)` / `UNICAST (не мне)` |
| **Что внутри?** | `type`, `data(utf8)`, `data(hex)` |

## Железо

| Компонент | Кол-во |
|-----------|--------|
| ESP32-WROOM-32 DevKit | 4 |
| LoRa Ra-01 (SX1278, 433 МГц) | 4 |
| Антенна 433 МГц | 4 (обязательно на каждом TX) |

### Распайка Ra-01 → ESP32 DevKit

| Ra-01 | ESP32 |
|-------|-------|
| 3.3V | 3.3V |
| GND | GND |
| SCK | GPIO 18 |
| MISO | GPIO 19 |
| MOSI | GPIO 23 |
| NSS | GPIO 5 |
| RST | GPIO 14 |
| DIO0 | GPIO 26 |

## Прошивка 4 узлов

1. Установить библиотеку **LoRa by Sandeep Mistry** (ZIP с GitHub — Library Manager часто даёт 403).
2. Открыть `test_firmware/LoRa_4Node_Test/LoRa_4Node_Test.ino`.
3. В начале файла изменить `#define MY_NODE_ID 1` на **1, 2, 3 или 4**.
4. Board: **ESP32 Dev Module**, Upload Speed 921600, Monitor **115200**.
5. Прошить каждый ESP32 со своим ID.

## Формат пакета

```
[dest_id][src_id][type][payload...][CRC16 LE]
```

| Поле | Значение |
|------|----------|
| `dest_id` | `0xFF` = broadcast всем; `1..4` = unicast конкретному ESP |
| `src_id` | `1..4` — кто отправил |
| `type` | `0x01` PING, `0x02` TEXT, `0x03` COMMAND, `0x04` STATUS |
| `payload` | произвольные байты (до 120) |
| `CRC16` | CRC-CCITT, little-endian |

## Команды Serial Monitor

| Команда | Действие |
|---------|----------|
| `HELP` | Справка |
| `STATUS` | ID узла, счётчики TX/RX |
| `BCAST привет всем` | Broadcast всем 4 ESP |
| `TO 3 только тебе` | Unicast только ESP #3 |
| `PING` | Broadcast ping (на PING unicast-адресат отвечает pong) |
| `AUTO ON` / `AUTO OFF` | Авто-ping каждые 5 с |
| `TYPE 0x03` | Тип сообщения для BCAST/TO |

## Пример сценария

1. Прошить 4 ESP с `MY_NODE_ID` = 1, 2, 3, 4.
2. Открыть Serial Monitor на каждом (4 COM-порта).
3. На ESP #1: `BCAST тест эфира`
   - Все 4 покажут: `ОТ КОГО: ESP-1`, `BROADCAST (для всех)`, текст payload.
4. На ESP #2: `TO 4 секрет для четвёртого`
   - ESP #4: `UNICAST (мне)` → обрабатывает.
   - ESP #1, #3: `UNICAST (не мне)` → только лог, не для них.
5. На ESP #3: `PING`
   - Все видят broadcast ping; узел-адресат (если unicast ping) ответит `pong from N`.

## Русский текст (UTF-8)

LoRa передаёт **байты как есть** — русский в UTF-8 (2 байта на букву) работает так же, как английский.

| Симптом | Причина |
|---------|---------|
| В логе `data(utf8): "привет"` читается нормально | Всё ок |
| Кракозябры вместо русского | Serial Monitor не в UTF-8 (редко в Arduino IDE 2) |
| Раньше были только точки `....` | Старая версия показывала в `ascii` только латиницу; смотрите `data(utf8)` или `data(hex)` |

**Проверка по hex:** слово «тест» в UTF-8 = `D1 82 D0 B5 D1 81 D1 82`.

**Arduino IDE 2:** ввод в Serial Monitor — UTF-8. Если кракозябры остаются, проверьте `data(hex)` на отправителе и получателе — байты должны совпадать.

## Диагностика

При `LoRa init FAILED` скетч выводит SPI-диагностику. **Swap-тест:** переставьте Ra-01 на другой ESP — если ошибка переехала с модулем, модуль неисправен.

## Отличие от старых скетчей

| | LoRa_Test_Sender/Receiver | LoRa_4Node_Test |
|--|---------------------------|-----------------|
| Узлов | 2 (TX + RX раздельно) | 4 (все двунаправленные) |
| ID | фиксированный sender=1 | `MY_NODE_ID` 1..4 |
| Unicast | нет | `TO <N>` |
| Фильтрация | нет | broadcast / unicast / не мне |
| CRC16 | нет | да |
| Команды Serial | нет | BCAST, TO, PING, AUTO |
