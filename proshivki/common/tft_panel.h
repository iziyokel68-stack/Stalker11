#ifndef STALKER_TFT_PANEL_H
#define STALKER_TFT_PANEL_H

#include <Arduino.h>
#include <SPI.h>

/**
 * M024-240320 on assembled PDA v3 — confirmed 17.08.2026.
 *
 * Glass is landscape 320×240. Controller behaves like ILI9342:
 * MADCTL bit MV is ignored, so Adafruit setRotation(1/3) leaves a
 * ~80 px dead strip (bottom). Rotation 0/2 (240×320) leaves the strip
 * on the right.
 *
 * Sequence: setRotation(1) so GFX is 320×240, then write MADCTL 0x36
 * without MV. 0x88 (MY|BGR) = full screen, not mirrored.
 *
 * Requires TFT_CS and TFT_DC before this include.
 *
 * Sketch: test_firmware/TFT_Standalone_Test/
 */

#define TFT_MADCTL_MY  0x80
#define TFT_MADCTL_MX  0x40
#define TFT_MADCTL_MV  0x20
#define TFT_MADCTL_BGR 0x08

#define TFT_GFX_ROTATION 1
#define TFT_MADCTL_PANEL (TFT_MADCTL_MY | TFT_MADCTL_BGR) /* 0x88 */
#define TFT_BGR_SWAP     1
#define TFT_SCR_W        320
#define TFT_SCR_H        240

#define TFT_SWAP_RB(c)                                                         \
  ((uint16_t)((((c)&0x001F) << 11) | ((c)&0x07E0) | (((c)&0xF800) >> 11)))

#ifndef TFT_CS
#error "Define TFT_CS and TFT_DC before including tft_panel.h"
#endif

static inline void tftWriteMadctl(uint8_t madctl) {
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  digitalWrite(TFT_CS, LOW);
  digitalWrite(TFT_DC, LOW);
  SPI.transfer(0x36);
  digitalWrite(TFT_DC, HIGH);
  SPI.transfer(madctl);
  digitalWrite(TFT_CS, HIGH);
  SPI.endTransaction();
}

template <typename T>
static inline void tftApplyPanel(T &tft) {
  pinMode(TFT_CS, OUTPUT);
  pinMode(TFT_DC, OUTPUT);
  tft.setRotation(TFT_GFX_ROTATION);
  tftWriteMadctl(TFT_MADCTL_PANEL);
}

#endif
