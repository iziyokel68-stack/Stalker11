/**
 * S.T.A.L.K.E.R. — TCA9548A channel map (PDA + external terminals)
 */
#ifndef STALKER_MUX_CHANNELS_H
#define STALKER_MUX_CHANNELS_H

#define TCA_ADDR_DEFAULT 0x70
#define EEPROM_ADDR_DEFAULT 0x50

#define MUX_CH_RESERVED_0  0
#define MUX_CH_UNIVERSAL   1
#define MUX_CH_ARMOR       2
#define MUX_CH_RESERVED_3  3
#define MUX_CH_ARTIFACT_1  4
#define MUX_CH_ARTIFACT_2  5
#define MUX_CH_ARTIFACT_3  6
#define MUX_CH_CASHIER_CABLE MUX_CH_UNIVERSAL
#define MUX_CH_SPARE_7     7

#endif // STALKER_MUX_CHANNELS_H
