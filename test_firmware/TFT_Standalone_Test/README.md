# TFT Standalone — полный экран M024 на плате ПДА v3

Эталон: `proshivki/common/tft_panel.h` (копия в этой папке).

## Что подтверждено (17.08.2026)

Стекло **320×240**. После `tft.begin()`:

```cpp
tftApplyPanel(tft);  // setRotation(1) + MADCTL 0x88 (MY|BGR), без MV
```

| Не делать | Полоса / артефакт |
|-----------|-------------------|
| только `setRotation(1)` или `(3)` | снизу ~80 px |
| `setRotation(0)` или `(2)` | справа ~80 px |
| MADCTL `0xC8` | весь экран, зеркало |

Пины платы: CS10 DC11 RST12 SCK13 MOSI14 MISO40. LoRa/BU03/вибро заглушены.

Board: **ESP32S3 Dev Module**, USB CDC On Boot = Enabled, Serial 115200.
