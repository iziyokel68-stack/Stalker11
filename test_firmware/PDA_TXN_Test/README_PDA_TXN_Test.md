# PDA TXN Test — минимальный тест ПДА для кассы

Прошивка без дисплея и радио. Опрашивает **CH1** (TXN @ `0x80`) как боевой `PDA_ESP32.ino`.

## Открыть в Arduino IDE

`E:\For_rabota\Projects\Stalker\test_firmware\PDA_TXN_Test\PDA_TXN_Test.ino`

В папке скетча 4 файла: `PDA_TXN_Test.ino` + `eeprom_*.h`, `mux_channels.h` (Arduino видит их как вкладки).

**Board:** ESP32S3 Dev Module (USB CDC On Boot = Enabled)  
**Monitor:** 115200

## Стенд

- EEPROM на **CH1** — тот же чип, что видит касса (кабель SDA/SCL или чип в слоте)
- EEPROM на **CH2** — броня (только ping в SCAN)

## Тест с кассой

1. Прошить **PDA_TXN_Test** на S3 ПДА.
2. Прошить **Terminal_ESP32** на WROOM-32.
3. Serial ПДА: `SET_MONEY:1000` `SET_LEVEL:2`
4. На кассе: `TXN_RESET` → `TXN_START:amount=100,item=1` → `TXN_WAIT`
5. На ПДА должно появиться: `>>> PURCHASE OK` и `TXN PURCHASE id=1 res=0`
6. Касса: `OK:TXN_DONE` или `OK:PURCHASE_DONE`

## Команды Serial (ПДА)

| Команда | Действие |
|---------|----------|
| `SCAN` | TCA + EEPROM на CH1/CH2 |
| `DUMP` | TXN-блок на CH1 |
| `STATUS` | баланс, уровень |
| `SET_MONEY:1000` | тестовые деньги |
| `SET_LEVEL:2` | уровень для магазина |
