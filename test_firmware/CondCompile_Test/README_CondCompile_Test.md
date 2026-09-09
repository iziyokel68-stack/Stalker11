# Тест условной компиляции (#ifdef)

Две минимальные прошивки проверяют, что ветки `#ifdef` из `Terminal_ESP32` / `Cashier_ESP32` собираются и на старте выдают **разные** профили и пины I2C.

## Прошивки

| № | Скетч | Профиль | I2C SDA/SCL | Плата в IDE |
|---|-------|---------|-------------|-------------|
| 1 | `test_firmware/CondCompile_WROOM32/CondCompile_WROOM32.ino` | `ESP32-WROOM32` | 21 / 22 | **ESP32 Dev Module** |
| 2 | `test_firmware/CondCompile_ESP32C3/CondCompile_ESP32C3.ino` | `ESP32-C3` | 5 / 6 | **ESP32C3 Dev Module** |

В каждом скетче задан принудительный `#define TERMINAL_BOARD_*` — так проверяется именно условная компиляция, а не только автодетект платы.

## Arduino IDE

| Параметр | WROOM32 | ESP32-C3 |
|----------|---------|----------|
| Board | ESP32 Dev Module | ESP32C3 Dev Module |
| Monitor | 115200 | 115200 |
| Upload Speed | 921600 (или 115200) | 921600 |

## Пошаговый тест

### 1. Компиляция

1. Открыть `CondCompile_WROOM32.ino` → Verify — **без ошибок**.
2. Открыть `CondCompile_ESP32C3.ino` → Verify — **без ошибок**.

Если `#error` вылезает — в IDE выбрана неподходящая плата.

### 2. Прошивка и Serial Monitor (115200)

**WROOM32** — ожидание:

```
STALKER:COND_COMPILE:v1,profile=ESP32-WROOM32,i2c_sda=21,i2c_scl=22,tca_mux=1
BRANCH:TCA_MUX_ENABLED
OK:READY
```

**ESP32-C3** — ожидание:

```
STALKER:COND_COMPILE:v1,profile=ESP32-C3,i2c_sda=5,i2c_scl=6,tca_mux=1
BRANCH:TCA_MUX_ENABLED
OK:READY
```

### 3. Команды Serial

| Команда | Ответ |
|---------|-------|
| `WHO` или `STALKER_WHO` | баннер профиля (как при старте) |
| `PING` | `PONG` |
| `HELP` | справка |

### 4. Визуально

- WROOM32: мигает встроенный LED (обычно GPIO 2).
- ESP32-C3 SuperMini: LED на GPIO 8.

## Дополнительно: ветка USE_TCA_MUX

В боевой прошивке терминала можно проверить вторую ветку:

- `#define USE_TCA_MUX 0` → при старте `BRANCH:DIRECT_I2C`
- `#define USE_TCA_MUX 1` (по умолчанию) → `BRANCH:TCA_MUX_ENABLED`

## Связанные файлы

- `proshivki/Terminal/Terminal_ESP32/Terminal_ESP32.ino` — боевой шаблон `#ifdef`
- `test_firmware/EEPROM_TCA_Test/` — авто-пины WROOM vs S3 (другая пара плат)
- `test_firmware/Terminal_Test/README_Terminal_Test.md` — сквозной тест терминала
