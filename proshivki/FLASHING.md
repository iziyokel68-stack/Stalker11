# Как прошивать устройства STALKER

Заливка `.ino` — один раз в Arduino IDE. Роль, урон, квесты, имя игрока — потом USB в программе мастера, без пересборки скетча.

Полный разбор «что умеет железо после заливки» — раздел **Что не заработает** ниже. Если пункт оттуда критичен для вашей сборки — не ждите, что «просто залил и всё».

---

## 1. Arduino IDE (ESP32)

1. Arduino IDE 2.x.
2. Плата: **Boards Manager → `esp32` by Espressif** (рекомендуется 2.0.x / 3.x, одна версия на все устройства).
3. Библиотеки **только для ПДА** (Library Manager):
   - Adafruit GFX Library
   - Adafruit ILI9341
   - DFRobot DFPlayer Mini
   - U8g2_for_Adafruit_GFX
4. Терминал, поле, CHIP_BOX — только стандартные `Wire` / `WiFi` / `Preferences` / `esp_now`.
5. Скетч открывать **File → Open** на файл `.ino` из репозитория (заголовки лежат рядом в той же папке). Не копировать один `.ino` в другую папку.

Upload не идёт на S3: скорость загрузки **115200**, другой кабель, BOOT+RESET.

---

## 2. Какие `.ino` заливать (боевые)

| Устройство | Файл | Плата в IDE | Tools (обязательно) |
| --- | --- | --- | --- |
| **ПДА** | `proshivki/Pda/V1/PDA_ESP32/PDA_ESP32.ino` | **ESP32S3 Dev Module** | Flash **16MB**, PSRAM **OPI**, USB CDC On Boot **Enabled**, Upload 115200 |
| **Аномалия / убежище** | `proshivki/Anomaly_GZ/Field_ESP32.ino` | **ESP32S3 Dev Module** | как у ПДА |
| **Терминал** (касса, банкомат, доска, допуск, банк) | `proshivki/Terminal/Terminal_ESP32/Terminal_ESP32.ino` | **ESP32 Dev Module** (WROOM) **или** **ESP32C3 Dev Module** | выбрать ту плату, что в корпусе; иначе `#error` |
| **CHIP_BOX** (чипы EEPROM) | `proshivki/Cip_Programmer/Chip_Programmer_ESP32.ino` | **ESP32C3 Dev Module** | I2C GPIO 5/6; USB CDC On Boot **Enabled** |

После заливки Serial **115200**. Ожидаемые строки:

| Устройство | Старт |
| --- | --- |
| ПДА | `STALKER:PDA:v2.7` (или соседняя v2.x) |
| Терминал | `STALKER:TERMINAL:v1,...` затем `READY` |
| Поле | `STALKER:ANOMALY:v1,id=…` или `SAFE_ZONE` |
| CHIP_BOX | `STALKER:CHIP_BOX:v1` |

Дальше USB в `stalker_app` → **Программатор**.

Не заливать на полигон: `proshivki/Cashier/` (старое имя кассы), всё из `test_firmware/`.

---

## 3. UWB (BU03-Kit) — это не ESP

Модуль BU03 — **отдельный STM32**. Arduino IDE его **не прошивает**. В репозитории **нет** `.bin` / `.hex` для STM.

| Действие | Как |
| --- | --- |
| Прошивка STM | Заводская AT-прошивка Ai-Thinker на модуле. Если модуль «мёртвый» (нет `OK` на `AT`) — не Arduino, а утилита производителя; образа у нас нет. |
| Связь с ESP | UART **115200**, строки с `\r\n`. На плате ПДА: ESP **Serial1** RX/TX **G1/G2**, питание ключ **G42**. |
| Боевой ПДА | Сам шлёт `AT` и `AT+DISTANCE` (страница UWB). Другие AT не нужны для «просто включил ПДА». |
| Калибровка метров | `AT+SETDEV a,b` — **на полигоне**, не при первой заливке ESP. |
| Смена ID / роли tag·anchor | `AT+SETCFG` — в **боевом** Field/PDA **нет**. Есть только в тестах `test_firmware/BU03_ID_Test.ino`, `UWB_*`. |

Проверка модуля без игры: `test_firmware/Test_PDA_Hardware/` (на собранном ПДА, `BU03_PINS_PLATE 1`) или `test_firmware/BU03_Distance_AT.ino` (макет часто G17/G18 — не путать с платой).

Канал экосистемы, если настраиваете AT вручную: `CH:1`, `Rate:1`, TWR `AT+SETUWBMODE=0`.

---

## 4. После заливки ESP (без этого устройство «пустое»)

| Устройство | Обязательно |
| --- | --- |
| ПДА | Программатор: функции/пресет. Имя — чип регистрации или `CONFIG:REGISTER`. Каждый старт — экран допуска, пока нет ADMIT. |
| Поле | `CONFIG_WRITE` с вкладки Аномалия или Убежище. Свежая заливка: `freq=0`, **урон не идёт**, пока не запишете частоту. |
| Терминал | Вкладка «Терминал»: роль, лимиты; для QUEST — каталог заданий. |
| CHIP_BOX | Только кабель к ПК; чипы пишет программатор. |

---

## 5. Что не заработает, даже если всё залито

Это не «чуть не доделали в IDE» — в боевых `.ino` этого кода нет.

| Нужно на полигоне | Факт |
| --- | --- |
| **Мастер-Пульт** (выброс/KILL по LoRa) | Отдельного `.ino` нет. CHIP_BOX на `LORA_TX` отвечает `ERROR:NO_LORA`. Выброс на ПДА — USB `CONFIG:EMISSION` или ESP-NOW, не пульт в эфире. |
| **LoRa на ПДА и поле** | Пины в спеке есть, `LoRa.begin` в боевых скетчах **нет** (только тесты). |
| **UWB-зоны / слоты аномалии** | ПДА меряет дистанцию на странице. Field **не** смотрит метры и бьёт broadcast. `ZONE_ASSIGN` / `AT+SETCFG` — только `test_firmware/UWB_*`. |
| **Экраны терминалов** | USB + кассета EEPROM. Корпусной TFT нет. |
| **Пины PCB V5** | Прошивки заточены под таблицу пинов в `MASTER_SPECIFICATION.md` §3 (архив v3). Если разводка V5 другая — заливка «как есть» не совпадёт с платой. Актуальный трек платы: `hardware/PCB_PROJECT.md`. |

Что **да**, после заливки + USB-конфига:

- ПДА: UI, чипы, TXN кабель CH0, ESP-NOW урон/хил/выброс-по-пакету, страница дистанции BU03.
- Терминал: касса/банк/квест/допуск по кабелю.
- Поле: broadcast аномалии или beacon убежища.
- CHIP_BOX: запись картриджей.

---

## 6. Стендовые `.ino` (не боевые)

| Зачем | Файл |
| --- | --- |
| Экран + LoRa + BU03 на одном S3 | `test_firmware/Test_PDA_Hardware/Test_PDA_Hardware.ino` |
| Только TFT | `test_firmware/TFT_Standalone_Test/` |
| TCA + EEPROM | `test_firmware/EEPROM_TCA_Test/` |
| LoRa ping (два DevKit) | `LoRa_Test_Sender` / `LoRa_Test_Receiver` — библиотека **LoRa (Sandeep Mistry)**, ZIP с GitHub |
| UWB слоты (прототип, не полигон) | `test_firmware/UWB_Anomaly_Test/`, `UWB_PDA_Test/` |

После стенда вернуть боевой `.ino` из таблицы §2.

---

## 7. Типичные сбои

| Симптом | Что проверить |
| --- | --- |
| Белый экран ПДА | CS/DC/RST G10–G14, 3V3; MADCTL — `tft_panel.h` |
| Терминал не компилируется | Выбрана не та плата (нужен WROOM или C3, не S3) |
| Поле молчит | Нет `CONFIG_WRITE` / `freq=0` |
| BU03 `NO SIGNAL` | G1/G2 не перепутаны, G42, заводская AT-прошивка |
| EEPROM не пишется | WP→GND, адрес 0x50, TCA канал CH0 |
| esptool «chip stopped responding» | Upload 115200, USB-C с линиями данных |
