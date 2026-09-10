/**
 * S.T.A.L.K.E.R. — TCA9548A channel map (PDA + external terminals)
 *
 * ARCHITECTURE: all player↔device interactions go through EEPROM cartridges on
 * TCA channels — not ESP-NOW / LoRa. ESP-NOW is reserved for combat (anomalies,
 * radiation, safe zones) only.
 *
 * PDA board: ESP32-S3 → I2C G9=SDA, G8=SCL → TCA9548A @ 0x70
 *
 * CH0      — universal adapter/consumable channel (peripherals, cashier
 *            cable, handoff chips). Chip header @0x00 for handoff chips
 *            (quest, consumable, admin). TXN block @0x80 for cable-linked
 *            terminals (cashier, ATM, quest board) — PDA polls here every
 *            ~200 ms (eeprom_txn.h).
 * CH2,3,5,6 — universal equipment slots. Each slot accepts ANY chip type
 *            (armor, artifact, or other device) — no longer fixed per slot;
 *            `applyChip()` decides behaviour from the chip's own `type` field.
 * CH1, CH4 — reserved (unused on PCB)
 * CH7      — spare
 *
 * Terminal topologies (all EEPROM, see eeprom_protocol.h):
 *   Handoff:  [Programmer/terminal]──I2C──[cartridge] → player inserts CH0
 *   Cable:    [Terminal ESP32]──I2C──[desk cassette]──4-wire──→ PDA CH0
 *
 * Only one I2C master may drive the bus at a time. Terminal writes when
 * txn state is IDLE; PDA polls CH0 every ~200 ms. State machine in eeprom_txn.h.
 */
#ifndef STALKER_MUX_CHANNELS_H
#define STALKER_MUX_CHANNELS_H

#define TCA_ADDR_DEFAULT 0x70
#define EEPROM_ADDR_DEFAULT 0x50

#define MUX_CH_UNIVERSAL   0   // adapters, consumables, admin, store/ATM/quest cable TXN
#define MUX_CH_RESERVED_1  1   // unused — reserved
#define MUX_CH_SLOT_1      2   // universal equipment slot (any chip type)
#define MUX_CH_SLOT_2      3   // universal equipment slot (any chip type)
#define MUX_CH_RESERVED_4  4   // unused — reserved
#define MUX_CH_SLOT_3      5   // universal equipment slot (any chip type)
#define MUX_CH_SLOT_4      6   // universal equipment slot (any chip type)
#define MUX_CH_CASHIER_CABLE MUX_CH_UNIVERSAL  // desk cassette cable → PDA CH0
#define MUX_CH_SPARE_7     7

#endif // STALKER_MUX_CHANNELS_H
