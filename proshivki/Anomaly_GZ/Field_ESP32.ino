/**
 * S.T.A.L.K.E.R. — Полевое устройство (Field_ESP32) v1.2
 * Аномалия (cat=0) / Убежище (cat=1)
 *
 * ESP-NOW + USB CONFIG + NVS. Если BU03 отвечает на AT — зоны по метрам.
 * Если нет BU03 — как раньше: broadcast (стенд без UWB).
 * LoRa: USB LORA_TX от программы мастера (пульт = ПК).
 *
 * Плата: ESP32S3 Dev Module. Пины как у ПДА (G1/G2 BU03, LoRa CS39 DIO0 41).
 * Serial 115200.
 */

#include <WiFi.h>
#include <esp_now.h>
#include <Preferences.h>
#include <SPI.h>
#include <string.h>

#pragma pack(push, 1)
struct Packet {
    uint8_t  emitter;
    uint8_t  msg_type;
    int16_t  val1;
    int16_t  val2;
    int16_t  val3;
};
#pragma pack(pop)

#include "zone_msgs.h"

#define LORA_SCK  13
#define LORA_MISO 40
#define LORA_MOSI 14
#define LORA_CS   39
#define LORA_DIO0 41
#include "lora_link.h"

#define EMITTER_ANOMALY  2
#define EMITTER_BASE     3
#define MSG_DAMAGE       1
#define MSG_RADIATION    3
#define MSG_ACK          6
#define MSG_SAFE_ZONE    7
#define SZ_PROT_FLAG     0x7FFF
#define SZ_BEACON_FLAG   0x00FF

#define BU03_RX 1
#define BU03_TX 2
#define BU03_PWR 42
#define HAS_BU03_PWR 1
#define AT_TIMEOUT_MS 1500
#define BOOT_WAIT_MS 2500
#define DIST_POLL_MS 1000

uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

Preferences prefs;

int cfgCat      = 0;
int cfgDmgMask  = 1;
int cfgDmg      = 10;
int cfgDmgMax   = 20;
int cfgDmgStep  = 1;
int cfgFreq     = 0;
int cfgRadDmg   = 0;
int cfgRadFrq   = 0;
int cfgRecharge = 0;
int cfgTarget   = 0;
int cfgErupt    = 30;
int cfgHits     = 3;
int cfgRadius   = 5;
int cfgUwbId    = 0;
int cfgSzHp     = 2;
int cfgSzHpFrq  = 30;
int cfgSzRad    = 1;
int cfgSzRadFrq = 60;
int cfgSzEmit   = 0;
int cfgSzProt[8] = {0};
char fieldId[8] = "000000";

bool loraOk = false;
bool bu03Ok = false;
bool uwbZones = false;

struct SlotEntry {
    bool used;
    uint8_t mac[6];
    uint32_t lastSeenMs;
};
SlotEntry slots[ZONE_MAX_SLOTS];

HardwareSerial bu03Serial(1);
String bu03Rx;
float lastDistM = -1.0f;
uint32_t lastDistMs = 0;

void initFieldId() {
    uint64_t mac = ESP.getEfuseMac();
    snprintf(fieldId, sizeof(fieldId), "%06X", (unsigned)(mac & 0xFFFFFF));
    if (cfgUwbId <= 0)
        cfgUwbId = (int)((mac & 0x0F) % 10) + 1;
}

void printWho() {
    String role = (cfgCat == 0) ? "ANOMALY" : "SAFE_ZONE";
    Serial.print("STALKER:");
    Serial.print(role);
    Serial.print(":v1,id=");
    Serial.println(fieldId);
}

void loadConfig() {
    prefs.begin("field", true);
    cfgCat      = prefs.getInt("cat",      0);
    cfgDmgMask  = prefs.getInt("dmg_mask", 1);
    cfgDmg      = prefs.getInt("dmg",      10);
    cfgDmgMax   = prefs.getInt("dmg_max",  20);
    cfgDmgStep  = prefs.getInt("dmg_stp",  1);
    cfgFreq     = prefs.getInt("freq",     0);
    cfgRadDmg   = prefs.getInt("rad_dmg",  0);
    cfgRadFrq   = prefs.getInt("rad_frq",  0);
    cfgRecharge = prefs.getInt("rech",     0);
    cfgTarget   = prefs.getInt("tgt",      0);
    cfgErupt    = prefs.getInt("erupt",    30);
    cfgHits     = prefs.getInt("hits",     3);
    cfgRadius   = prefs.getInt("radius",   5);
    cfgUwbId    = prefs.getInt("uwb_id",   0);
    cfgSzHp     = prefs.getInt("sz_hp",    2);
    cfgSzHpFrq  = prefs.getInt("sz_hp_f",  30);
    cfgSzRad    = prefs.getInt("sz_rad",   1);
    cfgSzRadFrq = prefs.getInt("sz_rad_f", 60);
    cfgSzEmit   = prefs.getInt("sz_emit",  0);
    for (int i = 0; i < 8; i++) {
        String key = "sz_p" + String(i);
        cfgSzProt[i] = prefs.getInt(key.c_str(), 0);
    }
    prefs.end();
}

void saveConfig() {
    prefs.begin("field", false);
    prefs.putInt("cat",      cfgCat);
    prefs.putInt("dmg_mask", cfgDmgMask);
    prefs.putInt("dmg",      cfgDmg);
    prefs.putInt("dmg_max",  cfgDmgMax);
    prefs.putInt("dmg_stp",  cfgDmgStep);
    prefs.putInt("freq",     cfgFreq);
    prefs.putInt("rad_dmg",  cfgRadDmg);
    prefs.putInt("rad_frq",  cfgRadFrq);
    prefs.putInt("rech",     cfgRecharge);
    prefs.putInt("tgt",      cfgTarget);
    prefs.putInt("erupt",    cfgErupt);
    prefs.putInt("hits",     cfgHits);
    prefs.putInt("radius",   cfgRadius);
    prefs.putInt("uwb_id",   cfgUwbId);
    prefs.putInt("sz_hp",    cfgSzHp);
    prefs.putInt("sz_hp_f",  cfgSzHpFrq);
    prefs.putInt("sz_rad",   cfgSzRad);
    prefs.putInt("sz_rad_f", cfgSzRadFrq);
    prefs.putInt("sz_emit",  cfgSzEmit);
    for (int i = 0; i < 8; i++) {
        String key = "sz_p" + String(i);
        prefs.putInt(key.c_str(), cfgSzProt[i]);
    }
    prefs.end();
}

void parseConfig(String cfg) {
    int start = 0;
    while (start < (int)cfg.length()) {
        int comma = cfg.indexOf(',', start);
        if (comma == -1) comma = cfg.length();
        String pair = cfg.substring(start, comma);
        int eq = pair.indexOf('=');
        if (eq > 0) {
            String key = pair.substring(0, eq);
            String val = pair.substring(eq + 1);
            key.trim(); val.trim();

            if      (key == "cat")         cfgCat      = val.toInt();
            else if (key == "dmg_mask")    cfgDmgMask  = val.toInt();
            else if (key == "dmg")         cfgDmg      = val.toInt();
            else if (key == "dmg_max")     cfgDmgMax   = val.toInt();
            else if (key == "dmg_stp")     cfgDmgStep  = val.toInt();
            else if (key == "freq")        cfgFreq     = max(1, (int)val.toInt());
            else if (key == "rad_dmg")     cfgRadDmg   = val.toInt();
            else if (key == "rad_frq")     cfgRadFrq   = val.toInt();
            else if (key == "rech")        cfgRecharge = val.toInt();
            else if (key == "tgt")         cfgTarget   = val.toInt();
            else if (key == "erupt")       cfgErupt    = val.toInt();
            else if (key == "hits")        cfgHits     = val.toInt();
            else if (key == "rad")         cfgRadius   = val.toInt();
            else if (key == "uwb_id")      cfgUwbId    = val.toInt();
            else if (key == "sz_hp")       cfgSzHp     = val.toInt();
            else if (key == "sz_hp_frq")   cfgSzHpFrq  = max(1, (int)val.toInt());
            else if (key == "sz_rad")      cfgSzRad    = val.toInt();
            else if (key == "sz_rad_frq")  cfgSzRadFrq = max(1, (int)val.toInt());
            else if (key == "sz_emission") cfgSzEmit   = val.toInt();
            else if (key.startsWith("sz_p")) {
                int idx = key.substring(4).toInt();
                if (idx >= 0 && idx <= 7)
                    cfgSzProt[idx] = constrain((int)val.toInt(), 0, 100);
            }
        }
        start = comma + 1;
    }
}

String buildConfigStr() {
    String s = "cat=" + String(cfgCat) + ",uwb_id=" + String(cfgUwbId) + ",";
    if (cfgCat == 0) {
        s += "dmg_mask=" + String(cfgDmgMask) + ",";
        s += "dmg="      + String(cfgDmg)      + ",";
        s += "dmg_max="  + String(cfgDmgMax)   + ",";
        s += "dmg_stp="  + String(cfgDmgStep)  + ",";
        s += "freq="     + String(cfgFreq)      + ",";
        s += "rad_dmg="  + String(cfgRadDmg)   + ",";
        s += "rad_frq="  + String(cfgRadFrq)   + ",";
        s += "rech="     + String(cfgRecharge) + ",";
        s += "tgt="      + String(cfgTarget)   + ",";
        s += "erupt="    + String(cfgErupt)    + ",";
        s += "hits="     + String(cfgHits)     + ",";
        s += "rad="      + String(cfgRadius);
    } else {
        s += "sz_hp="       + String(cfgSzHp)    + ",";
        s += "sz_hp_frq="   + String(cfgSzHpFrq) + ",";
        s += "sz_rad="      + String(cfgSzRad)   + ",";
        s += "sz_rad_frq="  + String(cfgSzRadFrq)+ ",";
        s += "sz_emission=" + String(cfgSzEmit)  + ",";
        for (int i = 0; i < 8; i++) {
            s += "sz_p" + String(i) + "=" + String(cfgSzProt[i]);
            if (i < 7) s += ",";
        }
        s += ",rad=" + String(cfgRadius);
    }
    return s;
}

String serialBuf = "";

void processCommand(String cmd) {
    cmd.trim();
    if (cmd.startsWith("CONFIG_WRITE:")) {
        parseConfig(cmd.substring(13));
        if (cfgUwbId <= 0)
            cfgUwbId = (int)((ESP.getEfuseMac() & 0x0F) % 10) + 1;
        saveConfig();
        Serial.println("OK:CONFIG_SAVED");
    }
    else if (cmd == "CONFIG_READ") {
        Serial.println("CONFIG:" + buildConfigStr());
    }
    else if (cmd == "STALKER_WHO") {
        printWho();
    }
    else if (cmd == "CONFIG:UID") {
        Serial.print("UID:");
        Serial.println(fieldId);
    }
    else if (cmd == "PING") {
        Serial.println("PONG");
    }
    else if (cmd.startsWith("LORA_TX:")) {
        if (!loraOk) {
            Serial.println("ERROR:NO_LORA");
            return;
        }
        if (stalkerHandleLoraTxLine(cmd, 0))
            Serial.println("OK");
        else
            Serial.println("ERROR:BAD_LORA");
    }
}

void handleSerial() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (serialBuf.length() > 0) {
                processCommand(serialBuf);
                serialBuf = "";
            }
        } else {
            serialBuf += c;
        }
    }
}

void ensurePeer(const uint8_t *mac) {
    if (esp_now_is_peer_exist(mac))
        return;
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 1;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
}

void sendPacketTo(const uint8_t *mac, uint8_t emitter, uint8_t msg,
                  int16_t v1, int16_t v2, int16_t v3) {
    Packet pkt;
    pkt.emitter  = emitter;
    pkt.msg_type = msg;
    pkt.val1     = v1;
    pkt.val2     = v2;
    pkt.val3     = v3;
    ensurePeer(mac);
    esp_now_send(mac, (uint8_t *)&pkt, sizeof(Packet));
}

void sendPacket(uint8_t emitter, uint8_t msg, int16_t v1, int16_t v2, int16_t v3) {
    sendPacketTo(broadcastMac, emitter, msg, v1, v2, v3);
}

void bu03Power(bool on) {
#if HAS_BU03_PWR
    pinMode(BU03_PWR, OUTPUT);
    digitalWrite(BU03_PWR, on ? HIGH : LOW);
    if (on) delay(BOOT_WAIT_MS);
#else
    (void)on;
#endif
}

void bu03Flush() {
    while (bu03Serial.available()) (void)bu03Serial.read();
}

bool bu03WaitResponse(uint32_t ms, bool needDistance) {
    bu03Rx = "";
    uint32_t t0 = millis();
    while (millis() - t0 < ms) {
        while (bu03Serial.available()) {
            char c = (char)bu03Serial.read();
            if (c == '\r') continue;
            if (bu03Rx.length() < 320) bu03Rx += c;
        }
        if (needDistance) {
            if (bu03Rx.indexOf("distance:") >= 0 && bu03Rx.indexOf("OK") >= 0)
                return true;
        } else if (bu03Rx.indexOf("OK") >= 0 || bu03Rx.indexOf("ERR") >= 0) {
            return true;
        }
        delay(1);
    }
    return bu03Rx.length() > 0;
}

bool bu03SendCmd(const char *cmd, bool needDistance) {
    bu03Flush();
    bu03Serial.print(cmd);
    bu03Serial.print("\r\n");
    return bu03WaitResponse(AT_TIMEOUT_MS, needDistance);
}

bool initBu03() {
    bu03Power(true);
    bu03Serial.setRxBufferSize(1024);
    bu03Serial.begin(115200, SERIAL_8N1, BU03_RX, BU03_TX);
    delay(400);
    bu03Flush();
    if (!bu03SendCmd("AT", false) || bu03Rx.indexOf("OK") < 0) {
        Serial.println("[BU03] NO LINK — зоны по UWB выкл, ESP-NOW broadcast");
        bu03Power(false);
        return false;
    }
    bu03SendCmd("AT+SETUWBMODE=0", false);
    char cmd[48];
    if (cfgCat == 0)
        snprintf(cmd, sizeof(cmd), "AT+SETCFG=%d,1,%d,%d", cfgUwbId, BU03_CH_POLY, BU03_RATE_POLY);
    else
        snprintf(cmd, sizeof(cmd), "AT+SETCFG=%d,0,%d,%d", cfgUwbId, BU03_CH_POLY, BU03_RATE_POLY);
    if (!bu03SendCmd(cmd, false) || bu03Rx.indexOf("ERR") >= 0) {
        Serial.println("[BU03] SETCFG FAIL");
        return false;
    }
    bu03SendCmd("AT+SAVE", false);
    Serial.printf("[BU03] OK uwb_id=%d role=%s\n", cfgUwbId, cfgCat == 0 ? "anchor" : "tag");
    return true;
}

bool pollDistance() {
    if (!bu03SendCmd("AT+DISTANCE", true)) return false;
    if (bu03Rx.indexOf("ERR") >= 0) return false;
    int i = bu03Rx.indexOf("distance:");
    if (i < 0) return false;
    lastDistM = bu03Rx.substring(i + 9).toFloat();
    lastDistMs = millis();
    return lastDistM >= 0.0f;
}

int findSlotByMac(const uint8_t *mac) {
    for (int i = 0; i < ZONE_MAX_SLOTS; i++) {
        if (slots[i].used && memcmp(slots[i].mac, mac, 6) == 0) return i;
    }
    return -1;
}

int allocSlot(const uint8_t *mac) {
    int idx = findSlotByMac(mac);
    if (idx >= 0) return idx;
    for (int i = 0; i < ZONE_MAX_SLOTS; i++) {
        if (!slots[i].used) {
            slots[i].used = true;
            memcpy(slots[i].mac, mac, 6);
            slots[i].lastSeenMs = millis();
            return i;
        }
    }
    return -1;
}

void releaseSlot(int idx) {
    if (idx < 0 || idx >= ZONE_MAX_SLOTS) return;
    slots[idx].used = false;
    memset(slots[idx].mac, 0, 6);
}

void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (len != sizeof(Packet)) return;
    Packet pkt;
    memcpy(&pkt, data, sizeof(Packet));

    if (cfgCat == 0 && pkt.msg_type == MSG_ZONE_HELLO) {
        int slot = allocSlot(info->src_addr);
        if (slot < 0) return;
        slots[slot].lastSeenMs = millis();
        sendPacketTo(info->src_addr, EMITTER_ANOMALY, MSG_ZONE_ASSIGN, slot, cfgUwbId, 0);
    } else if (cfgCat == 0 && pkt.msg_type == MSG_SLOT_READY) {
        int slot = findSlotByMac(info->src_addr);
        if (slot >= 0) slots[slot].lastSeenMs = millis();
    } else if (cfgCat == 0 && pkt.msg_type == MSG_SLOT_RELEASE) {
        int slot = findSlotByMac(info->src_addr);
        if (slot >= 0) releaseSlot(slot);
    } else if (pkt.msg_type == MSG_ACK) {
        int slot = findSlotByMac(info->src_addr);
        if (slot >= 0) slots[slot].lastSeenMs = millis();
    } else if (cfgCat == 1 && pkt.msg_type == MSG_ENTRY_OK) {
        /* ПДА подтвердил метры; хил всё равно beacon'ом, ПДА сам фильтрует */
    }
}

uint32_t lastDmgMs  = 0;
uint32_t lastRadMs  = 0;
uint32_t lastHealMs = 0;
uint32_t lastProtMs = 0;
int      protIdx    = 0;

bool inRadius() {
    if (!uwbZones) return true;
    return lastDistM >= 0.0f && lastDistM <= (float)cfgRadius;
}

void loopAnomaly() {
    uint32_t now = millis();

    if (uwbZones && now - lastDistMs >= DIST_POLL_MS) {
        lastDistMs = now;
        if (!pollDistance())
            lastDistM = -1.0f;
    }

    if (cfgFreq > 0 && (now - lastDmgMs >= (uint32_t)cfgFreq * 1000UL)) {
        lastDmgMs = now;
        if (!uwbZones) {
            sendPacket(EMITTER_ANOMALY, MSG_DAMAGE, cfgDmg, 0, cfgDmgMask);
            Serial.println("DMG broadcast dmg=" + String(cfgDmg));
        } else if (inRadius()) {
            int hits = 0;
            for (int i = 0; i < ZONE_MAX_SLOTS; i++) {
                if (!slots[i].used) continue;
                if (now - slots[i].lastSeenMs > ZONE_SLOT_TIMEOUT_MS) {
                    releaseSlot(i);
                    continue;
                }
                sendPacketTo(slots[i].mac, EMITTER_ANOMALY, MSG_DAMAGE, cfgDmg, 0, cfgDmgMask);
                hits++;
                if (cfgTarget != 0 && hits >= 1)
                    break;
            }
            Serial.printf("DMG uwb %.2fm slots=%d\n", lastDistM, hits);
        }
    }

    if (cfgRadDmg > 0 && cfgRadFrq > 0 &&
        (now - lastRadMs >= (uint32_t)cfgRadFrq * 1000UL)) {
        lastRadMs = now;
        if (!uwbZones) {
            sendPacket(EMITTER_ANOMALY, MSG_RADIATION, cfgRadDmg, 0, 0);
        } else if (inRadius()) {
            for (int i = 0; i < ZONE_MAX_SLOTS; i++) {
                if (!slots[i].used) continue;
                sendPacketTo(slots[i].mac, EMITTER_ANOMALY, MSG_RADIATION, cfgRadDmg, 0, 0);
            }
        }
    }
}

void loopSafeZone() {
    uint32_t now = millis();
    if (cfgSzHpFrq > 0 && (now - lastHealMs >= (uint32_t)cfgSzHpFrq * 1000UL)) {
        lastHealMs = now;
        sendPacket(EMITTER_BASE, MSG_SAFE_ZONE, cfgSzHp, cfgSzRad, 0);
    }
    if (now - lastProtMs >= 2000UL) {
        lastProtMs = now;
        sendPacket(EMITTER_BASE, MSG_SAFE_ZONE, cfgSzEmit, (int16_t)cfgRadius, SZ_BEACON_FLAG);
        if (cfgSzProt[protIdx] > 0) {
            sendPacket(EMITTER_BASE, MSG_SAFE_ZONE,
                       protIdx, cfgSzProt[protIdx], SZ_PROT_FLAG);
        }
        protIdx = (protIdx + 1) % 8;
    }
}

void setup() {
    Serial.begin(115200);
    delay(300);

    loadConfig();
    initFieldId();

    Serial.println("=== STALKER Field Device v1.2 ===");
    Serial.print("BEACON:id=");
    Serial.println(fieldId);
    printWho();
    Serial.println("CONFIG:" + buildConfigStr());
    if (cfgCat == 0 && cfgFreq <= 0)
        Serial.println("WARN: freq=0 — урон не шлётся, пока не CONFIG_WRITE");

    WiFi.mode(WIFI_STA);
    WiFi.setChannel(1);
    WiFi.disconnect();
    delay(100);

    if (esp_now_init() != ESP_OK) {
        Serial.println("ERROR: ESP-NOW init failed");
        return;
    }
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, broadcastMac, 6);
    peer.channel = 1;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
    esp_now_register_recv_cb(OnDataRecv);

    loraOk = stalkerLoraBegin();
    Serial.println(loraOk ? "LoRa OK (USB LORA_TX)" : "LoRa FAIL");

    bu03Ok = initBu03();
    uwbZones = bu03Ok && cfgCat == 0;
    if (cfgCat == 1 && bu03Ok)
        Serial.println("UWB tag (убежище) — ПДА меряет вход");

    Serial.println("READY");
}

void loop() {
    handleSerial();
    if (loraOk) {
        Packet dummy;
        char text[4];
        stalkerLoraPoll(0, &dummy, text, sizeof(text));
    }
    if (cfgCat == 0) loopAnomaly();
    else              loopSafeZone();
}
