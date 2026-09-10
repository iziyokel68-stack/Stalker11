# Cashier → Terminal (переименование)

Прошивка кассы перенесена в универсальный терминал:

**`proshivki/Terminal/Terminal_ESP32/Terminal_ESP32.ino`**

Старая папка `Cashier/` сохранена для совместимости ссылок в документации.  
Новая прошивка отвечает `STALKER:TERMINAL:v1` (не `STALKER:CASHIER:v1`).

Роли: `STORE` (касса), `ATM`, `QUEST`, `ADMIT`, `BANK` — через USB `TERMINAL_ROLE` и лимиты `TERMINAL_CFG` (программатор, без перезаливки .ino).

Тест: `test_firmware/Terminal_Test/README_Terminal_Test.md`
