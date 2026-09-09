/**
 * S.T.A.L.K.E.R. — TCA9548A channel map (PDA + external terminals)
 *
 * ARCHITECTURE: all player↔device interactions go through EEPROM cartridges on
 * TCA channels — not ESP-NOW / LoRa. ESP-NOW is reserved for combat (anomalies,
 * radiation, safe zones) only.
 *
 * PDA board: ESP32-S3 → I2C G9=SDA, G8=SCL → TCA9548A @ 0x70
 *
 * CH0, CH3 — reserved (unused on PCB)
 * CH1      — universal player slot (TRRS / J_TRS ribbon; cashier cable here)
 *            Chip header @0x00 for handoff chips (quest, consumable, admin).
 *            TXN block @0x80 for cable-linked terminals (cashier, ATM, quest
 *            board) — PDA polls here every ~200 ms (eeprom_txn.h).
 * CH2      — armor (TYPE_ARMOR chip @0x00; PDA reads on insert — Phase 5)
 * CH4..CH6 — artifacts 1..3 (TYPE_ARTIFACT)
 * CH7      — spare
 *
 * Terminal topologies (all EEPROM, see eeprom_protocol.h):
 *   Handoff:  [Programmer/terminal]──I2C──[cartridge] → player inserts CH1
 *   Cable:    [Terminal ESP32]──I2C──[desk cassette]──4-wire──→ PDA CH1
 *
 * Only one I2C master may drive the bus at a time. Terminal writes when
 * txn state is IDLE; PDA polls CH1 every ~200 ms. State machine in eeprom_txn.h.
 */
#ifndef STALKER_MUX_CHANNELS_H
#define STALKER_MUX_CHANNELS_H

#define TCA_ADDR_DEFAULT 0x70
#define EEPROM_ADDR_DEFAULT 0x50

#define MUX_CH_RESERVED_0  0   // unused — reserved
#define MUX_CH_UNIVERSAL   1   // consumables, admin, store/ATM/quest cable TXN
#define MUX_CH_ARMOR       2   // TYPE_ARMOR chip @0x00
#define MUX_CH_RESERVED_3  3   // unused — reserved
#define MUX_CH_ARTIFACT_1  4   // TYPE_ARTIFACT
#define MUX_CH_ARTIFACT_2  5
#define MUX_CH_ARTIFACT_3  6
#define MUX_CH_CASHIER_CABLE MUX_CH_UNIVERSAL  // desk cassette cable → PDA CH1
#define MUX_CH_SPARE_7     7

#endif // STALKER_MUX_CHANNELS_H
