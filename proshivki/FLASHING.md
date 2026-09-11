# Как прошивать устройства STALKER

Заливка `.ino` — один раз в Arduino IDE. Роль, урон, квесты, имя игрока — потом USB в **программе мастера** (`stalker_app`), без пересборки скетча.

**Мастер-пульт = программа на ПК**, не отдельный ESP с экраном. Выброс / KILL / радио: USB `LORA_TX` на **ПДА или поле** (S3 + Ra-01). CHIP_BOX (C3) радио не имеет → `ERROR:NO_LORA`.

STM на BU03 — **отдельная** заливка: `BU03_AT.md` (разные модули — разные AT-образы).

---

## 1. Arduino IDE (ESP32)

1. Arduino IDE 2.x.
2. Плата: **Boards Manager → `esp32` by Espressif** (рекомендуется 2.0.x / 3.x, одна версия на все устройства).
3. Библиотеки **для ПДА** (Library Manager):
   - Adafruit GFX Library
   - Adafruit ILI9341
   - DFRobot DFPlayer Mini
   - U8g2_for_Adafruit_GFX
4. Библиотека **LoRa (Sandeep Mistry)** — и ПДА, и поле. Library Manager либо ZIP с GitHub (`sandeepmistry/arduino-LoRa`). Без неё скетч не соберётся (`lora_link.h`).
5. Терминал и CHIP_BOX — стандартные `Wire` / `WiFi` / `Preferences` / `esp_now` (без LoRa).
6. Скетч открывать **File → Open** на файл `.ino` из репозитория (заголовки лежат **в той же папке**). Не копировать один `.ino` в другую папку. Эталон `proshivki/common/*.h` дублируется в папке скетча — Arduino не видит `../common`.

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
| ПДА | `STALKER:PDA:v2.8` |
| Терминал | `STALKER:TERMINAL:v1,...` затем `READY` |
| Поле | `STALKER:ANOMALY:v1,id=…` или `SAFE_ZONE` |
| CHIP_BOX | `STALKER:CHIP_BOX:v1` |

Дальше USB в `stalker_app` → **Программатор** (конфиг) или **Оповещения** (LoRa, если на USB ПДА/поле с Ra-01).

Не заливать на полигон: `proshivki/Cashier/` (старое имя кассы), всё из `test_firmware/`.

---

## 3. UWB (BU03) — STM, не ESP

Полная инструкция заливки AT на STM32: **`BU03_AT.md`**. Кратко:

| Действие | Как |
| --- | --- |
| Прошивка STM | Не Arduino. Образ AT — у вендора модуля (в репо нет `.bin`). BOOT0+RESET → CubeProgrammer / UART / ST-Link. |
| Связь с ESP | UART **115200** `\r\n`. Плата ПДА/поля: Serial1 **G1/G2**, питание **G42**. |
| Боевой код | Поле: слоты `ZONE_*`, `AT+SETCFG`, урон unicast если `AT+DISTANCE` ≤ радиус. ПДА: HELLO / ASSIGN / tag; у ЗЗ — anchor и вход по метрам. Нет `OK` на `AT` — поле бьёт **broadcast** (стенд без модуля). |
| Калибровка метров | `AT+SETDEV a,b` на полигоне, см. `BU03_AT.md`. |
| Канал экосистемы | `CH:1`, `Rate:1`, TWR `AT+SETUWBMODE=0` (если команда есть в вашей AT). |

Проверка модуля без игры: `test_firmware/Test_PDA_Hardware/` (`BU03_PINS_PLATE 1`) или USB-UART + ручной `AT`.

---

## 4. После заливки ESP (без этого устройство «пустое»)

| Устройство | Обязательно |
| --- | --- |
| ПДА | Программатор: функции/пресет. Имя — чип регистрации или `CONFIG:REGISTER`. Каждый старт — экран допуска, пока нет ADMIT. |
| Поле | `CONFIG_WRITE` с вкладки Аномалия или Убежище (`uwb_id`, радиус). Свежая заливка: `freq=0`, **урон не идёт**, пока не запишете частоту. |
| Терминал | Вкладка «Терминал»: роль, лимиты; для QUEST — каталог заданий. |
| CHIP_BOX | Только кабель к ПК; чипы пишет программатор. LoRa с этого USB нет. |

---

## 5. Что уже в боевых `.ino`, и чего всё ещё нет

| Нужно на полигоне | Сейчас |
| --- | --- |
| **Пульт мастера** | `stalker_app` на ПК. USB `LORA_TX` → ПДА **или** поле с Ra-01. Отдельного `.ino` пульта нет и не будет. |
| **LoRa на ПДА и поле** | Есть (`lora_link.h`, 433 МГц, RST не трогаем — G12 = TFT). Библиотека LoRa (Sandeep Mistry). |
| **UWB-зоны** | Есть: `ZONE_HELLO` / `ASSIGN` / `SLOT_*` / `ENTRY_OK`. Один замер `AT+DISTANCE` на якоре на все слоты. Две аномалии рядом (`SLOT_LEASE`) — не сделано. |
| **CHIP_BOX** | Без радио, так и задумано. |
| **Экраны терминалов** | USB + кассета EEPROM. Корпусной TFT нет. |
| **Пины PCB V5** | Прошивки под таблицу в `MASTER_SPECIFICATION.md` §3. Если разводка V5 другая — не совпадёт. Трек платы: `hardware/PCB_PROJECT.md`. |

Что **да**, после заливки + USB-конфига:

- ПДА: UI, чипы, TXN CH0, ESP-NOW, LoRa RX/TX, UWB-слоты и вход в ЗЗ по метрам.
- Терминал: касса/банк/квест/допуск по кабелю.
- Поле: зоны UWB или broadcast без модуля; LoRa как USB-радио мастера.
- CHIP_BOX: запись картриджей.

---

## 6. Стендовые `.ino` (не боевые)

| Зачем | Файл |
| --- | --- |
| Экран + LoRa + BU03 на одном S3 | `test_firmware/Test_PDA_Hardware/Test_PDA_Hardware.ino` |
| Только TFT | `test_firmware/TFT_Standalone_Test/` |
| TCA + EEPROM | `test_firmware/EEPROM_TCA_Test/` |
| LoRa ping (два DevKit) | `LoRa_Test_Sender` / `LoRa_Test_Receiver` |
| UWB слоты (прототип) | `test_firmware/UWB_Anomaly_Test/`, `UWB_PDA_Test/` |

После стенда вернуть боевой `.ino` из таблицы §2.

---

## 7. Типичные сбои

| Симптом | Что проверить |
| --- | --- |
| Белый экран ПДА | CS/DC/RST G10–G14, 3V3; MADCTL — `tft_panel.h` |
| Терминал не компилируется | Выбрана не та плата (нужен WROOM или C3, не S3) |
| Поле / ПДА: `lora_link.h: LoRa.h` | нет библиотеки **LoRa (Sandeep Mistry)** |
| Поле молчит | Нет `CONFIG_WRITE` / `freq=0` |
| BU03 `NO LINK` | G1/G2, G42, AT-прошивка STM — `BU03_AT.md` |
| EEPROM не пишется | WP→GND, адрес 0x50, TCA канал CH0 |
| esptool «chip stopped responding» | Upload 115200, USB-C с линиями данных |
| `ERROR:NO_LORA` в программе | На USB CHIP_BOX/терминал. Переключите кабель на ПДА или поле с Ra-01. |
