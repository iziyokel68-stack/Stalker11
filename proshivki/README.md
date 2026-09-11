# Прошивки устройств STALKER

**Как заливать:** `proshivki/FLASHING.md` (Arduino IDE, платы, UWB, что не заработает без доработки).

Один раз `.ino` в IDE. Роль, лимиты, задания, аномалия, убежище, чип — USB в программе мастера, без пересборки.

| Железка | Скетч | Плата в IDE |
| --- | --- | --- |
| ПДА | `Pda/V1/PDA_ESP32/PDA_ESP32.ino` | ESP32S3 Dev Module (N16R8) |
| Терминал | `Terminal/Terminal_ESP32/Terminal_ESP32.ino` | ESP32 Dev Module или ESP32-C3 |
| Аномалия / убежище | `Anomaly_GZ/Field_ESP32.ino` | ESP32S3 Dev Module |
| CHIP_BOX (стол мастера: чипы + LoRa) | `Cip_Programmer/Chip_Programmer_ESP32.ino` | ESP32-S3 или ESP32-C3 |

`Cashier/` — старое имя кассы, не заливать. `test_firmware/` — только стенд.

Serial-команды терминала: `Terminal/TERMINAL_COMMANDS.md`.
