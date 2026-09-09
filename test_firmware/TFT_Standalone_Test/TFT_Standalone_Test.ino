/**
 * TFT full-screen check — assembled PDA v3 (M024)
 * tft_panel.h: rotation 1 + MADCTL 0x88 (confirmed 17.08.2026)
 */

#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <SPI.h>

#define TFT_CS   10
#define TFT_DC   11
#define TFT_RST  12
#define TFT_SCK  13
#define TFT_MOSI 14
#define TFT_MISO 40
#include "tft_panel.h"

#define LORA_CS    39
#define LORA_DIO0  41
#define BU03_PWR   42
#define PIN_VIBRO  21
#define LED_RED    15
#define LED_GREEN  16
#define DF_TX      17
#define DF_RX      18
#define PIN_SDA    9
#define PIN_SCL    8
#define BTN_DN     4
#define BTN_RT     5
#define BTN_OK     6
#define BTN_ESC    7

#define C_BLACK   0x0000
#if TFT_BGR_SWAP
#define C_RED     0x001F
#define C_BLUE    0xF800
#else
#define C_RED     0xF800
#define C_BLUE    0x001F
#endif
#define C_GREEN   0x07E0
#define C_WHITE   0xFFFF
#define C_YELLOW  0xFFE0
#define C_CYAN    0x07FF
#define C_MAGENTA 0xF81F
#define C_ORANGE  0xFD20
#define C_DGRAY   0x4208
#define C_LGRAY   0xC618
#define C_MIDGRAY 0x8410

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

uint8_t page = 0;
uint32_t lastMs = 0;

void stubUnusedHardware() {
  pinMode(LORA_CS, OUTPUT);
  digitalWrite(LORA_CS, HIGH);
  pinMode(LORA_DIO0, INPUT);
  pinMode(BU03_PWR, OUTPUT);
  digitalWrite(BU03_PWR, LOW);
  pinMode(PIN_VIBRO, OUTPUT);
  digitalWrite(PIN_VIBRO, LOW);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GREEN, LOW);
  pinMode(DF_TX, INPUT);
  pinMode(DF_RX, INPUT);
  pinMode(PIN_SDA, INPUT);
  pinMode(PIN_SCL, INPUT);
  pinMode(BTN_DN, INPUT_PULLUP);
  pinMode(BTN_RT, INPUT_PULLUP);
  pinMode(BTN_OK, INPUT_PULLUP);
  pinMode(BTN_ESC, INPUT_PULLUP);
}

void clearAll(uint16_t c) {
  tftApplyPanel(tft);
  tft.fillRect(0, 0, TFT_SCR_W, TFT_SCR_H, c);
}

void drawFrameBorder() {
  tft.drawRect(0, 0, 320, 240, C_YELLOW);
  tft.drawRect(1, 1, 318, 238, C_YELLOW);
  tft.fillRect(0, 0, 8, 8, C_RED);
  tft.fillRect(312, 0, 8, 8, C_GREEN);
  tft.fillRect(0, 232, 8, 8, C_BLUE);
  tft.fillRect(312, 232, 8, 8, C_MAGENTA);
}

void drawRulers() {
  for (int x = 0; x < 320; x += 20) {
    int t = (x % 100 == 0) ? 10 : 5;
    tft.drawFastVLine(x, 0, t, C_CYAN);
    tft.drawFastVLine(x, 240 - t, t, C_CYAN);
  }
  for (int y = 0; y < 240; y += 20) {
    int t = (y % 100 == 0) ? 10 : 5;
    tft.drawFastHLine(0, y, t, C_ORANGE);
    tft.drawFastHLine(320 - t, y, t, C_ORANGE);
  }
  tft.setTextSize(1);
  tft.setTextColor(C_WHITE);
  tft.setCursor(10, 12);
  tft.print("x0");
  tft.setCursor(292, 12);
  tft.print("x319");
  tft.setCursor(10, 220);
  tft.print("y239");
}

void drawPageMain() {
  clearAll(C_BLACK);
  const uint16_t bars[] = {C_WHITE, C_YELLOW, C_CYAN, C_GREEN,
                           C_MAGENTA, C_RED, C_BLUE, C_BLACK};
  for (int i = 0; i < 8; i++)
    tft.fillRect(i * 40, 4, 40, 22, bars[i]);

  tft.fillRect(4, 30, 100, 28, C_RED);
  tft.fillRect(110, 30, 100, 28, C_GREEN);
  tft.fillRect(216, 30, 100, 28, C_BLUE);

  for (int row = 0; row < 20; row += 5)
    for (int col = 0; col < 312; col += 5) {
      bool on = ((col / 5) + (row / 5)) & 1;
      tft.fillRect(4 + col, 62 + row, 5, 5, on ? C_LGRAY : C_DGRAY);
    }

  tft.fillRect(4, 88, 312, 84, C_DGRAY);
  tft.drawRect(4, 88, 312, 84, C_CYAN);
  tft.setTextSize(1);
  tft.setTextColor(C_CYAN);
  tft.setCursor(10, 94);
  tft.print("320x240  MADCTL no MV");
  tft.setTextColor(C_WHITE);
  tft.setCursor(10, 110);
  tft.printf("gfx %dx%d  madctl=0x%02X", tft.width(), tft.height(), TFT_MADCTL_PANEL);
  tft.setCursor(10, 124);
  tft.print("MADCTL 0x88  still mirror -> 0x48");
  tft.setCursor(10, 138);
  tft.printf("CS%d DC%d RST%d SCK%d MOSI%d", TFT_CS, TFT_DC, TFT_RST, TFT_SCK,
             TFT_MOSI);
  tft.setCursor(10, 152);
  tft.setTextColor(C_GREEN);
  tft.print("yellow frame must touch all 4 bezels");

  tft.drawFastHLine(0, 120, 320, C_WHITE);
  tft.drawFastVLine(160, 0, 240, C_WHITE);
  tft.drawCircle(160, 120, 30, C_YELLOW);

  drawRulers();
  drawFrameBorder();
}

void drawPageGrid() {
  clearAll(C_BLACK);
  for (int x = 0; x < 320; x += 10)
    tft.drawFastVLine(x, 0, 240, (x % 50 == 0) ? C_CYAN : C_DGRAY);
  for (int y = 0; y < 240; y += 10)
    tft.drawFastHLine(0, y, 320, (y % 50 == 0) ? C_YELLOW : C_DGRAY);
  drawFrameBorder();
  tft.setTextSize(2);
  tft.setTextColor(C_WHITE);
  tft.setCursor(80, 100);
  tft.print("GRID 10px");
  tft.setTextSize(1);
  tft.setCursor(90, 128);
  tft.printf("320x240  MADCTL 0x%02X", TFT_MADCTL_PANEL);
}

void drawPageColors() {
  clearAll(C_BLACK);
  const uint16_t cols[] = {C_RED, C_GREEN, C_BLUE, C_WHITE, C_YELLOW, C_CYAN};
  const char *names[] = {"RED", "GREEN", "BLUE", "WHITE", "YELLOW", "CYAN"};
  for (int i = 0; i < 6; i++) {
    tft.fillRect(i * 53, 0, (i == 5) ? 55 : 53, 240, cols[i]);
    tft.setTextSize(1);
    tft.setTextColor((i == 3 || i == 4) ? C_BLACK : C_WHITE);
    tft.setCursor(i * 53 + 6, 110);
    tft.print(names[i]);
  }
  drawFrameBorder();
}

void drawPage(uint8_t p) {
  tftApplyPanel(tft);
  switch (p % 3) {
  case 0:
    drawPageMain();
    Serial.println("[PAGE] main 320x240 no-MV");
    break;
  case 1:
    drawPageGrid();
    Serial.println("[PAGE] grid");
    break;
  case 2:
    drawPageColors();
    Serial.println("[PAGE] colors");
    break;
  }
  Serial.printf("[STATE] %dx%d MADCTL=0x%02X\n", tft.width(), tft.height(),
                TFT_MADCTL_PANEL);
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println();
  Serial.println("=== PDA TFT 320x240 native (no MV) ===");
  Serial.printf("MADCTL=0x%02X (no MV). Upside-down -> 0x08\n", TFT_MADCTL_PANEL);

  stubUnusedHardware();
  pinMode(TFT_CS, OUTPUT);
  pinMode(TFT_DC, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, -1);
  tft.begin();
  tftApplyPanel(tft);

  drawPage(0);
  lastMs = millis();
}

void loop() {
  if (millis() - lastMs < 4000)
    return;
  lastMs = millis();
  page++;
  drawPage(page);
}
