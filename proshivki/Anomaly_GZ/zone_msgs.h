/**
 * ESP-NOW зональные пакеты (UWB). msg_type в том же 8-байт Packet.
 * Эталон. Копии — в папках скетчей Field и PDA.
 */
#ifndef STALKER_ZONE_MSGS_H
#define STALKER_ZONE_MSGS_H

#define MSG_ZONE_HELLO   20
#define MSG_ZONE_ASSIGN  21
#define MSG_SLOT_READY   22
#define MSG_SLOT_RELEASE 23
#define MSG_ENTRY_OK     24

#define ZONE_MAX_SLOTS      8
/* BU03 AT: ID обычно 0–10, TWR по одному. ЗЗ на толпу — вход UWB, дальше ESP-NOW. */
#define ZONE_HELLO_MS       1000
#define ZONE_SLOT_TIMEOUT_MS 8000
#define BU03_CH_POLY        1
#define BU03_RATE_POLY      1

#endif
