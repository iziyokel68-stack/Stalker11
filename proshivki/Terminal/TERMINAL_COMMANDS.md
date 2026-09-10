# STALKER Terminal ESP32 — Serial-команды

Справочник по протоколу `Terminal_ESP32.ino` (`processCommand`).  
Источники: прошивка + `programmat_pc/programmer.py`.

## Параметры линии

| Параметр | Значение |
|----------|----------|
| Скорость | **115200** бод |
| Окончание строки | `\n` или `\r` (оба принимаются) |
| Формат | одна команда на строку, без префиксов |

При загрузке ESP32 выводит баннер, текущую роль, статус I2C/EEPROM и строку `READY`.

---

## Discovery / диагностика

| Команда | Ответ | Описание |
|---------|-------|----------|
| `STALKER_WHO` | `STALKER:TERMINAL:v1,role=STORE,op_default=0,name=Касса` | Идентификация устройства (тип, версия, роль, op по умолчанию). |
| `PING` | `PONG` | Проверка связи. **PC-программатор не отправляет.** |
| `I2C_SCAN` | `I2C_SCAN:begin` → строки `0xNN` → `I2C_SCAN:end count=N (...)` | Сканирование шины I2C (через TCA9548A, канал кассеты). **PC не отправляет.** |
| `EEPROM_PING` | `EEPROM:OK addr=0x50` + `EEPROM:TXN_OK TXN:...` или ошибка | Проверка EEPROM @0x50 и блока транзакции. **PC не отправляет.** |

Возможные ошибки EEPROM: `EEPROM:MISSING`, `EEPROM:READ_FAIL`, `EEPROM:TXN_BAD_CRC`.

Неизвестная команда: `ERROR:UNKNOWN_CMD`.

---

## Конфигурация роли

Роль хранится в NVS (`terminal_role`) и переживает перезагрузку.

| Команда | Ответ | Описание |
|---------|-------|----------|
| `TERMINAL_ROLE` | `TERMINAL_ROLE:STORE,op_default=0,name=Касса` | Текущая роль (без изменения). |
| `TERMINAL_ROLE:STORE` | `OK:TERMINAL_ROLE:STORE,op_default=0,name=Касса` | Установить роль. Имя регистронезависимо. |
| `TERMINAL_ROLE:???` | `ERROR:BAD_ROLE` | Неизвестная роль. |

---

## TXN-команды (EEPROM-транзакции)

Блок транзакции: 36 байт @ EEPROM `0x0080`. PDA читает/обновляет блок по I2C.

> **Важно:** `TXN_START` принимается **только с двоеточием** — `TXN_START:...`.  
> Строка `TXN_START` без `:` → `ERROR:UNKNOWN_CMD`.

| Команда | Описание |
|---------|----------|
| `TXN_START:amount=N,item=M[,параметры]` | Создать транзакцию в состоянии PENDING и записать в EEPROM. |
| `TXN_STATUS` | Прочитать блок; ответ — строка `TXN:state=...` (без префикса OK/ERROR). |
| `TXN_WAIT` | Ждать SUCCESS/FAILED (таймаут по умолчанию 30000 мс). |
| `TXN_WAIT:timeout_ms=N` | То же с явным таймаутом. |
| `TXN_RESET` | Сбросить блок в IDLE. |

### Параметры `TXN_START`

Пары `ключ=значение` через запятую (порядок произвольный):

| Ключ | Описание |
|------|----------|
| `amount` | Сумма в рублях (для PURCHASE/BANK/ATM/QUEST — награда для QUEST). |
| `item` | ID предмета / код счёта / категория квеста (`quest_cat_id`). |
| `op` | Код операции (см. таблицу ниже). Если **не указан** — берётся `op_default` текущей роли. |
| `flags` | Битовая маска флагов (см. ниже). |
| `txn_id` | ID транзакции; если ≤0 или отсутствует — автоинкремент на терминале. |
| `quest_id` | Префикс ID квеста (до 8 символов), только для `op=QUEST`. |

### Ответы TXN

| Ответ | Значение |
|-------|----------|
| `OK:TXN_PENDING:TXN:...` | Транзакция записана, ждёт PDA. |
| `OK:TXN_DONE:TXN:...` | Успех (из `TXN_WAIT`). |
| `ERROR:TXN_FAILED:TXN:...` | Отказ PDA (из `TXN_WAIT`). |
| `OK:TXN_IDLE` | Сброс выполнен. |
| `ERROR:EEPROM_NOT_FOUND` | EEPROM не найден. |
| `ERROR:READ_FAILED` / `ERROR:WRITE_FAILED` | Ошибка I2C. |
| `ERROR:TXN_BUSY:TXN:...` | Блок не в IDLE. |
| `ERROR:BAD_AMOUNT` | `amount` ≤ 0 (где обязателен). |
| `ERROR:BAD_OP` | Неизвестный `op`. |
| `ERROR:BAD_CRC` | Невалидный блок (при `TXN_STATUS`). |
| `ERROR:TIMEOUT` | PDA не ответил за `timeout_ms`. |
| `ERROR:RESET_FAILED` | Не удалось записать сброс. |

Формат статуса `TXN:...`:

```
TXN:state=N,txn_id=ID,amount=A,item=I,paid=P,balance=B,result=R,result_name=NAME,op=O,op_name=NAME,flags=F
```

---

## Коды операций (`op`)

| Код | Имя | Роль по умолчанию | Параметры `TXN_START` |
|-----|-----|-------------------|------------------------|
| 0 | PURCHASE | STORE | `amount` > 0, `item` = ID товара |
| 1 | BANK | BANK | `amount` > 0, `item` = код счёта; `flags` bit 0x02 = депозит |
| 2 | QUEST | QUEST | `item` = категория, `amount` = награда; `quest_id`; `flags` bit 0x04 = завершение |
| 3 | ADMIT | ADMIT | только `txn_id` (amount/item не нужны) |
| 4 | ATM | ATM | как BANK: `amount` > 0, `item` = код счёта |

Для BANK и QUEST флаги задаются внутри билдера; для остальных op произвольный `flags` записывается в блок, если `flags ≠ 0`.

---

## Флаги (`flags`)

| Бит | Константа | Применение |
|-----|-----------|------------|
| 0x01 | `TXN_FLAG_DISCOUNT` | Скидка (PURCHASE и др., если передан) |
| 0x02 | `TXN_FLAG_BANK_DEPOSIT` | BANK/ATM: депозит (иначе снятие) |
| 0x04 | `TXN_FLAG_QUEST_COMPLETE` | QUEST: отметка завершения квеста |

---

## Состояния и результаты

**Состояния (`state`):** 0 IDLE, 1 PENDING, 2 PROCESSING, 3 SUCCESS, 4 FAILED.

**Результаты (`result` / `result_name`):**

| Код | Имя |
|-----|-----|
| 0 | OK |
| 1 | INSUFFICIENT_FUNDS |
| 2 | LEVEL_TOO_LOW |
| 3 | SYSTEM_LOCKED |
| 4 | BAD_CRC |
| 5 | BAD_MAGIC |
| 6 | QUEST_NOT_FOUND |
| 7 | QUEST_DUPLICATE |
| 8 | NOT_IMPLEMENTED |
| 99 | UNKNOWN |

---

## Таблица ролей

| Роль | Отображение | `op_default` | Операция |
|------|-------------|--------------|----------|
| STORE | Касса | 0 | PURCHASE |
| ATM | Банкомат | 4 | ATM |
| QUEST | Квестовая доска | 2 | QUEST |
| ADMIT | Допуск | 3 | ADMIT |
| BANK | Банк | 1 | BANK |

---

## Каталог доски заданий (роль QUEST)

Мастер прошивает кассету с ПК (`stalker_app` → «Прошить доску»). `title=` — **последний** ключ, в значении допустимы пробелы и запятые.

| Команда | Ответ | Описание |
|---------|-------|----------|
| `QUEST_CATALOG_CLEAR` | `OK:QUEST_CATALOG_CLEAR` | Очистить RAM-каталог (ещё не EEPROM). |
| `QUEST_ADD:id=Q01,rub=300,mode=timeout,timeout_min=120,hidden=0,title=…` | `OK:QUEST_ADD:Q01` | Добавить/заменить карточку. `mode`: `timeout` / `oneshot` / `shared`. |
| `QUEST_CATALOG_COMMIT` | `OK:QUEST_CATALOG_COMMIT:count=N` | Записать полный каталог @`0x0800` и висящие карточки @`0x0100`. |
| `QUEST_DUMP` | `QUEST_DUMP:count=N` + строки `QUEST:…` | Показать RAM-каталог. |
| `QUEST_CLAIMS` | `QUEST_CLAIMS:count=N` + строки `CLAIM:…` | Слоты выдачи (NVS). |
| `QUEST_CLAIMS_CLEAR` | `OK:QUEST_CLAIMS_CLEAR` | Сбросить выдачи, пересобрать список на кассете. |

`TXN_START` для QUEST на занятом timeout/oneshot: `ERROR:QUEST_TAKEN`.

ПДА пишет заявку TQ @`0x00A4`; терминал сам ставит QUEST TXN, после SUCCESS обновляет слот и listed-каталог.

---

## Примеры

```
STALKER_WHO
TERMINAL_ROLE:STORE
PING

TXN_START:amount=500,item=1
TXN_START:amount=1000,item=42,op=1,flags=2
TXN_START:amount=200,item=3,op=2,quest_id=Q12,flags=4
TXN_START:amount=0,item=0,op=3
TXN_STATUS
TXN_WAIT:timeout_ms=60000
TXN_RESET

QUEST_CATALOG_CLEAR
QUEST_ADD:id=Q01,rub=300,mode=timeout,timeout_min=120,hidden=0,title=Найти ПДА
QUEST_CATALOG_COMMIT
QUEST_DUMP
QUEST_CLAIMS

I2C_SCAN
EEPROM_PING
```

Покупка в роли STORE (op не указывается):

```
TERMINAL_ROLE:STORE
TXN_START:amount=500,item=1
TXN_WAIT
```

---

## Подмножество PC-программатора (`programmer.py`)

Программатор использует **115200**, добавляет `\n` к каждой команде.

| Когда | Команды |
|-------|---------|
| Подключение | `STALKER_WHO`; при необходимости `TERMINAL_ROLE` (чтение роли с устройства) |
| UI: смена роли | `TERMINAL_ROLE:{STORE\|ATM\|QUEST\|ADMIT\|BANK}` |
| Кнопки TXN | `TXN_START:amount={A},item={I}[,txn_id={ID}]` |
| | `TXN_STATUS` |
| | `TXN_WAIT:timeout_ms={N}` (только TERMINAL / CASHIER) |
| | `TXN_RESET` |

**Не отправляется с ПК:** `PING`, `I2C_SCAN`, `EEPROM_PING` (только ручная отладка / тестовый режим прошивки).

PC не передаёт `op`, `flags`, `quest_id` — тип транзакции определяется **текущей ролью** терминала. Опционально только `txn_id` при `txn_id > 0`.
