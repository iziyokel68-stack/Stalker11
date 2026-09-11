/**
 * Эталон. Копии лежат в папках скетчей (Arduino не видит ../common).
 *
 * LoRa Ra-01 на ESP32-S3: общий SPI с TFT, RST не трогаем (G12 = TFT).
 * Кадр: dest src kind payload crc16-le
 * kind 0 = 8 байт игрового Packet; kind 1 = текст broadcast.
 * dest 0 или 0xFF = всем.
 *
 * Перед include: Packet, LORA_CS, LORA_DIO0; опц. TFT_CS, LORA_SCK/MISO/MOSI.
 */
#ifndef STALKER_LORA_LINK_H
#define STALKER_LORA_LINK_H

#include <LoRa.h>

#ifndef LORA_FREQ_HZ
#define LORA_FREQ_HZ 433E6
#endif
#define LORA_KIND_PKT  0
#define LORA_KIND_TEXT 1

static uint16_t stalkerLoraCrc(const uint8_t *d, int n) {
  uint16_t c = 0xFFFF;
  for (int i = 0; i < n; i++) {
    c ^= d[i];
    for (int b = 0; b < 8; b++)
      c = (c & 1) ? (uint16_t)((c >> 1) ^ 0xA001) : (uint16_t)(c >> 1);
  }
  return c;
}

static void stalkerLoraIdleCs() {
  pinMode(LORA_CS, OUTPUT);
  digitalWrite(LORA_CS, HIGH);
#ifdef TFT_CS
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
#endif
}

static bool stalkerLoraBegin() {
  stalkerLoraIdleCs();
#if defined(LORA_SCK) && defined(LORA_MISO) && defined(LORA_MOSI)
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
#endif
  LoRa.setPins(LORA_CS, -1, LORA_DIO0);
  if (!LoRa.begin(LORA_FREQ_HZ))
    return false;
  LoRa.setSyncWord(0x12);
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setTxPower(17);
  return true;
}

static void stalkerLoraSendPkt(uint8_t dest, uint8_t src, const Packet &pkt) {
  stalkerLoraIdleCs();
  uint8_t buf[2 + 1 + 8];
  buf[0] = dest;
  buf[1] = src;
  buf[2] = LORA_KIND_PKT;
  memcpy(buf + 3, &pkt, 8);
  uint16_t crc = stalkerLoraCrc(buf, 11);
  LoRa.beginPacket();
  LoRa.write(buf, 11);
  LoRa.write((uint8_t)(crc & 0xFF));
  LoRa.write((uint8_t)(crc >> 8));
  LoRa.endPacket();
  stalkerLoraIdleCs();
}

static void stalkerLoraSendText(uint8_t dest, uint8_t src, const char *text) {
  stalkerLoraIdleCs();
  uint8_t n = 0;
  if (text) {
    while (text[n] && n < 48)
      n++;
  }
  uint8_t hdr[4] = {dest, src, LORA_KIND_TEXT, n};
  uint8_t tmp[4 + 48];
  memcpy(tmp, hdr, 4);
  if (n)
    memcpy(tmp + 4, text, n);
  uint16_t crc = stalkerLoraCrc(tmp, 4 + n);
  LoRa.beginPacket();
  LoRa.write(tmp, 4 + n);
  LoRa.write((uint8_t)(crc & 0xFF));
  LoRa.write((uint8_t)(crc >> 8));
  LoRa.endPacket();
  stalkerLoraIdleCs();
}

/** true если пакет нам (dest 0/0xFF или dest==myId). textOut[0]=0 если не текст. */
static bool stalkerLoraPoll(uint8_t myId, Packet *pktOut, char *textOut, int textCap) {
  if (textOut && textCap > 0)
    textOut[0] = 0;
  stalkerLoraIdleCs();
  int sz = LoRa.parsePacket();
  if (sz < 6)
    return false;
  uint8_t buf[64];
  if (sz > (int)sizeof(buf))
    sz = sizeof(buf);
  int n = LoRa.readBytes(buf, sz);
  if (n < 6)
    return false;
  uint16_t crc = (uint16_t)buf[n - 2] | ((uint16_t)buf[n - 1] << 8);
  if (stalkerLoraCrc(buf, n - 2) != crc)
    return false;
  uint8_t dest = buf[0];
  if (!(dest == 0 || dest == 0xFF || dest == myId))
    return false;
  uint8_t kind = buf[2];
  if (kind == LORA_KIND_PKT) {
    if (n < 13 || !pktOut)
      return false;
    memcpy(pktOut, buf + 3, 8);
    return true;
  }
  if (kind == LORA_KIND_TEXT && textOut && textCap > 1) {
    uint8_t ln = buf[3];
    if (ln > n - 6)
      ln = (uint8_t)(n - 6);
    if (ln >= textCap)
      ln = (uint8_t)(textCap - 1);
    memcpy(textOut, buf + 4, ln);
    textOut[ln] = 0;
    return true;
  }
  return false;
}

/** LORA_TX:to=N,msg=EMISSION|COMMAND|RADIO|BROADCAST,v1=,v2=[,text=] */
static bool stalkerHandleLoraTxLine(const String &line, uint8_t srcId) {
  if (!line.startsWith("LORA_TX:"))
    return false;
  String body = line.substring(8);
  int dest = 0;
  int v1 = 0, v2 = 0;
  String msg = "COMMAND";
  String text = "";
  int start = 0;
  while (start < (int)body.length()) {
    int comma = body.indexOf(',', start);
    if (comma < 0)
      comma = body.length();
    String pair = body.substring(start, comma);
    int eq = pair.indexOf('=');
    if (eq > 0) {
      String k = pair.substring(0, eq);
      String v = pair.substring(eq + 1);
      k.trim();
      v.trim();
      if (k == "to")
        dest = v.toInt();
      else if (k == "v1")
        v1 = v.toInt();
      else if (k == "v2")
        v2 = v.toInt();
      else if (k == "msg")
        msg = v;
      else if (k == "text")
        text = v;
    }
    start = comma + 1;
  }
  uint8_t d = (uint8_t)constrain(dest, 0, 255);
  msg.toUpperCase();
  if (msg == "BROADCAST") {
    stalkerLoraSendText(d == 0 ? 0xFF : d, srcId, text.c_str());
    return true;
  }
  Packet pkt;
  pkt.emitter = 0;
  pkt.val1 = (int16_t)v1;
  pkt.val2 = (int16_t)v2;
  pkt.val3 = 0;
  if (msg == "EMISSION")
    pkt.msg_type = 8;
  else if (msg == "RADIO")
    pkt.msg_type = 11;
  else if (msg == "HEAL")
    pkt.msg_type = 2;
  else
    pkt.msg_type = 5; // COMMAND
  stalkerLoraSendPkt(d == 0 ? 0xFF : d, srcId, pkt);
  return true;
}

#endif
