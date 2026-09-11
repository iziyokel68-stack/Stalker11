# StalkerCommon — Arduino-библиотека заголовков EEPROM/TCA

Эталон исходников прошивки: `proshivki/common/` (`eeprom_txn.h`, `eeprom_protocol.h`, `mux_channels.h`, `tft_panel.h`).

## Установка для Arduino IDE

Скопируйте эту папку в:

`C:\Users\<ИМЯ>\Documents\Arduino\libraries\StalkerCommon`

После копирования **перезапустите Arduino IDE**.

В скетчах:

```cpp
#include <eeprom_protocol.h>
#include <mux_channels.h>
#include <tft_panel.h>   // после #define TFT_CS / TFT_DC
```

`tftApplyPanel(tft)` — сразу после `tft.begin()` (M024: MADCTL 0x88, без MV).

## Скетчи проекта

| Скетч | Путь |
|-------|------|
| Терминал (касса, ATM, …) | `proshivki/Terminal/Terminal_ESP32/Terminal_ESP32.ino` |
| ПДА | `proshivki/Pda/V1/PDA_ESP32/` |
| Тест экрана (полный кадр) | `test_firmware/TFT_Standalone_Test/` |
| Тест ПДА + касса | `test_firmware/PDA_TXN_Test/PDA_TXN_Test.ino` |

Открывайте `.ino` **File → Open** из репозитория. Как заливать: `proshivki/FLASHING.md`.
