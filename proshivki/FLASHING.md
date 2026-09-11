# Как прошивать устройства STALKER

Заливка `.ino` — один раз в Arduino IDE. Роль, урон, квесты, имя игрока — потом USB в **программе мастера** (`stalker_app`), без пересборки скетча.

**Стол мастера** — один стационарный ESP, постоянно на USB ноутбука. Это и пульт LoRa, и программатор чипов, и регистрация / снятие данных в конце игры. Скетч: `Chip_Programmer_ESP32.ino` (`STALKER:CHIP_BOX:v2`). `ERROR:NO_LORA` значит **на этом ESP нет живого Ra-01**, а не «нужен другой кабель».

Полевые касса / банкомат / доска — отдельные терминалы, не стол мастера.

---

## 1. Arduino IDE (ESP32)

1. Arduino IDE 2.x.
2. Плата: **Boards Manager → `esp32` by Espressif** (рекомендуется 2.0.x / 3.x, одна версия на все устройства).
3. Библиотеки **для ПДА** (Library Manager):
   - Adafruit GFX Library
   - Adafruit ILI9341
   - DFRobot DFPlayer Mini
   - U8g2_for_Adafruit_GFX
4. Библиотека **LoRa (Sandeep Mistry)** — ПДА, поле и **стол мастера (CHIP_BOX)**. Library Manager либо ZIP с GitHub (`sandeepmistry/arduino-LoRa`). Без неё скетч не соберётся (`lora_link.h`).
5. Терминал в поле — стандартные `Wire` / `WiFi` / `Preferences` / `esp_now` (без LoRa).
6. Скетч открывать **File → Open** на файл `.ino` из репозитория (заголовки лежат **в той же папке**). Не копировать один `.ino` в другую папку. Эталон `proshivki/common/*.h` дублируется в папке скетча — Arduino не видит `../common`.

Upload не идёт на S3: скорость загрузки **115200**, другой кабель, BOOT+RESET.

---

## 2. Какие `.ino` заливать (боевые)

| Устройство | Файл | Плата в IDE | Tools (обязательно) |
| --- | --- | --- | --- |
| **Стол мастера** (пульт + чипы + регистрация) | `proshivki/Cip_Programmer/Chip_Programmer_ESP32.ino` | **ESP32S3 Dev Module** (рекомендуется) **или** **ESP32C3 Dev Module** | USB CDC On Boot **Enabled**, Upload 115200; S3: Flash **16MB**. EEPROM I2C **GPIO 5/6**. LoRa: S3 CS39 DIO0 41 SCK13 MISO40 MOSI14; C3 CS10 DIO0 3 SCK4 MISO2 MOSI7 |
| **ПДА** | `proshivki/Pda/V1/PDA_ESP32/PDA_ESP32.ino` | **ESP32S3 Dev Module** | Flash **16MB**, PSRAM **OPI**, USB CDC On Boot **Enabled**, Upload 115200 |
| **Аномалия / убежище** | `proshivki/Anomaly_GZ/Field_ESP32.ino` | **ESP32S3 Dev Module** | как у ПДА |
| **Терминал** (касса, банкомат, доска, допуск, банк **в поле**) | `proshivki/Terminal/Terminal_ESP32/Terminal_ESP32.ino` | **ESP32 Dev Module** (WROOM) **или** **ESP32C3 Dev Module** | выбрать ту плату, что в корпусе; иначе `#error` |

После заливки Serial **115200**. Ожидаемые строки:

| Устройство | Старт |
| --- | --- |
| Стол мастера | `STALKER:CHIP_BOX:v2` и `LoRa OK` |
| ПДА | `STALKER:PDA:v2.8` |
| Терминал | `STALKER:TERMINAL:v1,...` затем `READY` |
| Поле | `STALKER:ANOMALY:v1,id=…` или `SAFE_ZONE` |

Дальше USB в `stalker_app`: программатор чипов, регистрация, оповещения / команды — всё на этом же COM-порту.

Не заливать на полигон: `proshivki/Cashier/` (старое имя кассы), всё из `test_firmware/`.

---

## 3. UWB (BU03) — STM, не ESP

ESP32 **не** прошивает модуль UWB. На BU03 свой контроллер (обычно **STM32F103**). Arduino IDE его не видит. В репозитории **нет** `.bin` / `.hex`: у разных партий и клонов — **разные** образы AT.

Боевые скетчи ПДА и поля говорят с модулем только UART **115200**, строки с `\r\n`. Если на `AT` нет `OK` — сначала этот раздел, не пины ESP.

### 3.1 Что вы прошиваете

| Слой | Чем | Файл |
| --- | --- | --- |
| ESP32-S3 (ПДА / поле / стол) | Arduino IDE | `.ino` из `proshivki/` |
| STM32 на BU03 | STM32CubeProgrammer / FlyMcu / ST-Link | `.bin` / `.hex` **вашего** вендора |

Нужна прошивка **с AT-командами**, не «голый» пример TWR из Keil SDK. Если залить SDK-демо без AT, ESP будет слать `AT` в пустоту.

Официальный SDK Ai-Thinker (если собираете сами в Keil): [STM32F103-BU0x_SDK](https://github.com/Ai-Thinker-Open/STM32F103-BU0x_SDK). Готовый AT-образ берите у поставщика модуля / с диска к Kit — **не из этого репозитория**.

### 3.2 Как войти в загрузчик STM32

На **BU03-Kit** (плата с кнопками BOOT и RESET):

1. Зажать **BOOT** (BOOT0 = 1).
2. Нажать и отпустить **RESET**.
3. Отпустить **BOOT**.
4. STM в системном загрузчике: USB DFU и/или UART ISP.

На голом модуле без кнопок: вывод BOOT0 на 3V3, импульс NRST, потом BOOT0 на GND для обычного старта.

Не оставляйте BOOT0 поднятым после прошивки — модуль снова уйдёт в загрузчик и не ответит на `AT`.

### 3.3 Чем лить образ

**A. STM32CubeProgrammer + USB DFU**

1. [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html).
2. Войти в загрузчик (§3.2). Кабель в USB Kit.
3. Интерфейс **USB**, Refresh, Connect.
4. Open file → ваш `.bin` / `.hex` / `.elf`.
5. Address для `.bin` обычно `0x08000000` (Flash STM32F1).
6. Start Programming, галка **Run after programming**.
7. BOOT0 = 0, Reset. UART 115200 — должен ответить на `AT`.

**B. UART ISP** (USB-UART к TX/RX Kit)

Тот же загрузчик. CubeProgrammer → **UART**, 115200; для ROM bootloader F1 часто even parity (если не коннектится — 8E1). Либо **FlyMcu**: порт, `.hex`, «начать программирование» при зажатом BOOT + RESET.

**C. ST-Link / J-Link (SWD)**

Пин-хедер Kit: **SWDIO**, **SWCLK**, GND, 3V3. CubeProgrammer → **ST-LINK**. Не кормите модуль 5V и ST-Link 3V3 одновременно без общей земли.

### 3.4 После заливки: какой это AT

Разные прошивки — разный набор команд. Снимите отпечаток **до** калибровки и до игры.

USB-UART к UART модуля (**не** к ESP), 115200, перевод строки **CRLF** (`\r\n`):

```
AT
AT+VERSION
AT+HELP
AT+GETCFG
```

**Диалект, который ждут боевые `.ino`**

| Команда | Зачем |
| --- | --- |
| `AT` → `OK` | жив ли UART |
| `AT+SETUWBMODE=0` | TWR (если команды нет — ESP игнорирует ошибку) |
| `AT+SETCFG=<id>,<role>,<ch>,<rate>` | id **0–10**, **role 0=tag / 1=anchor** |
| `AT+SAVE` | запомнить в STM |
| `AT+DISTANCE` | на **anchor**: строка с `distance:` и `OK` |
| `AT+GETCFG` | проверка ID/роли |
| `AT+SETDEV=<a>,<b>` | калибровка метров **на полигоне** |
| `AT+RESTART` | мягкая перезагрузка STM |

Полигон: **CH:1**, **Rate:1**, TWR.

- аномалия = **anchor** (role=1), свой `uwb_id`;
- ПДА у аномалии = **tag** (role=0), слот выдаёт поле;
- убежище = **tag** с фиксированным id; ПДА **на входе** = **anchor id=0**.

ESP на ПДА/поле: Serial1 **G1/G2**, питание ключ **G42**.

Клоны часто дают `AT+RANGE` вместо `AT+DISTANCE`. Тогда либо AT-образ с таблицей выше, либо поле без `OK` на `AT` уйдёт в ESP-NOW **broadcast**. Не смешивайте два диалекта на одном полигоне.

### 3.5 Сколько человек одновременно (ЗЗ на 30 человек)

BU03 в AT — **не Wi‑Fi на толпу**.

| Ограничение | Факт |
| --- | --- |
| ID в экосистеме | обычно **0–10** (спека Ai-Thinker / наши `SETCFG`) |
| TWR | **одна** сессия дальномера за раз; `AT+DISTANCE` даёт **одно** число |
| Слоты аномалии в прошивке | **8** (`ZONE_MAX_SLOTS`) |
| 30 ПДА как якоря с **одним** ID 0 | коллизия эфира, ложные метры, вход/выход «мигает» |

Поэтому **зелёная зона на толпу**:

1. UWB — только **дверь** (1–2 человека меряют вход).
2. После успешного порога ПДА **защёлкивает** «я внутри» и **отпускает** якорь id=0.
3. Пока слышен ESP-NOW beacon убежища — хил/защита, хоть их 30.
4. Выход — пропал beacon (ушли из радиотени точки), не «30 дальномеров сразу».

Аномалия: якорь один (поле), теги — слоты 0–7. Девятый и дальше слот не получат, unicast урона им не будет. Для 30 человек у одной аномалии UWB-фильтр не рассчитан — либо несколько точек, либо без BU03 (broadcast).

### 3.6 Калибровка метров (`AT+SETDEV`)

Не прошивка ESP. На каждой точке по методике Excel/доки Ai-Thinker: `AT+SETDEV a,b`, затем `AT+SAVE`. Пока не откалибровано, `AT+DISTANCE` врёт — радиус в программаторе будет «не тот».

### 3.7 Сбои BU03

| Симптом | Что проверить |
| --- | --- |
| ESP: `[BU03] NO LINK` | TX/RX G1/G2, G42 = 1, 3V3, BOOT0 не в загрузчике |
| `AT` молчит после заливки | не AT-образ / BOOT0 всё ещё 1 / другой UART Kit |
| `SETCFG` → `ERR` | чужой диалект или id вне 0–10 |
| `DISTANCE` без `distance:` | опрос не на anchor, или команда `RANGE` |
| Две аномалии, странный урон | у якорей **разные** `uwb_id`; канал/rate одинаковые |

Проверка без игры: `test_firmware/Test_PDA_Hardware/` или USB-UART + ручной `AT`.

---

## 4. После заливки ESP (без этого устройство «пустое»)

| Устройство | Обязательно |
| --- | --- |
| Стол мастера | USB в `stalker_app`. Чипы, мост регистрации, LoRa. Нет `LoRa OK` — припаяйте/проверьте Ra-01. |
| ПДА | Программатор: функции/пресет. Имя — чип регистрации (стол) или `CONFIG:REGISTER`. Каждый старт — экран допуска, пока нет ADMIT. |
| Поле | `CONFIG_WRITE` с вкладки Аномалия или Убежище (`uwb_id`, радиус). Свежая заливка: `freq=0`, **урон не идёт**, пока не запишете частоту. |
| Терминал в поле | Вкладка «Терминал»: роль, лимиты; для QUEST — каталог заданий. |

---

## 5. Что уже в боевых `.ino`, и чего всё ещё нет

| Нужно на полигоне | Сейчас |
| --- | --- |
| **Стол мастера** | CHIP_BOX v2: EEPROM + TXN + LoRa `LORA_TX`. Программа — `stalker_app`. |
| **LoRa на ПДА и поле** | Есть (`lora_link.h`, 433 МГц, RST не трогаем — G12 = TFT). |
| **UWB-зоны** | Вход по метрам; ЗЗ на толпу — защёлка + ESP-NOW. Аномалия: 8 слотов, один `AT+DISTANCE`. `SLOT_LEASE` двух аномалий — нет. |
| **Экраны терминалов** | USB + кассета EEPROM. Корпусной TFT нет. |
| **Пины PCB V5** | Прошивки под `MASTER_SPECIFICATION.md` §3. Трек платы: `hardware/PCB_PROJECT.md`. |

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
| `lora_link.h: LoRa.h` | нет библиотеки **LoRa (Sandeep Mistry)** |
| Поле молчит | Нет `CONFIG_WRITE` / `freq=0` |
| BU03 `NO LINK` | G1/G2, G42, AT STM — §3 выше |
| EEPROM не пишется | WP→GND, адрес 0x50, SDA/SCL 5/6 |
| esptool «chip stopped responding» | Upload 115200, USB-C с линиями данных |
| `ERROR:NO_LORA` | На столе мастера не завёлся Ra-01 (`LoRa FAIL`). Не переключайте кабель на ПДА игрока. |
