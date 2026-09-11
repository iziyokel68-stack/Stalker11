/**
 * S.T.A.L.K.E.R. — Полевое устройство (Field_ESP32) v1.1
 * Универсальная прошивка: Аномалия (cat=0) / Убежище (cat=1)
 *
 * Что делает этот .ino: ESP-NOW broadcast (урон / хил / beacon) + USB CONFIG_* + NVS.
 * Чего нет: LoRa, BU03, слоты ZONE_*, гейтинг по метрам (cfgRadius в NVS не используется).
 *
 * Плата в IDE: ESP32S3 Dev Module (как ПДА). Пины LoRa/BU03 в коде не трогаются.
 *
 * Заливка:
 *   1. Этот файл — один раз через Arduino IDE.
 *   2. USB → программатор: CONFIG_WRITE:cat=0|1,... (иначе аномалия молчит: freq=0).
 *   3. Конфиг в NVS, переживает выключение.
 *
 * Serial 115200.
 */

#include <WiFi.h>
#include <esp_now.h>
#include <Preferences.h>

// ─────────────────────────────────────────────────────
//  ПРОТОКОЛ (совпадает с PDA_ESP32.ino)
// ─────────────────────────────────────────────────────
#pragma pack(push, 1)
struct Packet {
    uint8_t  emitter;
    uint8_t  msg_type;
    int16_t  val1;
    int16_t  val2;
    int16_t  val3;
};
#pragma pack(pop)

#define EMITTER_ANOMALY  2   // Emitter.ANOMALY = 2 (по протоколу protocol.py)
#define EMITTER_BASE     3   // Emitter.BASE = 3
#define MSG_DAMAGE       1
#define MSG_RADIATION    3
#define MSG_SAFE_ZONE    7
#define SZ_PROT_FLAG     0x7FFF   // val3 в пакете защиты убежища
#define SZ_BEACON_FLAG   0x00FF   // val3 в beacon-пакете (только обновление флага зоны)

uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ─────────────────────────────────────────────────────
//  КОНФИГ
// ─────────────────────────────────────────────────────
Preferences prefs;

int cfgCat      = 0;
// Аномалия
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
// Убежище
int cfgSzHp     = 2;
int cfgSzHpFrq  = 30;
int cfgSzRad    = 1;
int cfgSzRadFrq = 60;
int cfgSzEmit   = 0;
int cfgSzProt[8] = {0};
char fieldId[8] = "000000";

void initFieldId() {
    uint64_t mac = ESP.getEfuseMac();
    snprintf(fieldId, sizeof(fieldId), "%06X", (unsigned)(mac & 0xFFFFFF));
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

// ─────────────────────────────────────────────────────
//  РАЗБОР КОНФИГА (key=value через запятую)
// ─────────────────────────────────────────────────────
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
    String s = "cat=" + String(cfgCat) + ",";
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

// ─────────────────────────────────────────────────────
//  SERIAL
// ─────────────────────────────────────────────────────
String serialBuf = "";

void processCommand(String cmd) {
    cmd.trim();
    if (cmd.startsWith("CONFIG_WRITE:")) {
        parseConfig(cmd.substring(13));
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

// ─────────────────────────────────────────────────────
//  ESP-NOW
// ─────────────────────────────────────────────────────
void sendPacket(uint8_t emitter, uint8_t msg, int16_t v1, int16_t v2, int16_t v3) {
    Packet pkt;
    pkt.emitter  = emitter;
    pkt.msg_type = msg;
    pkt.val1     = v1;
    pkt.val2     = v2;
    pkt.val3     = v3;
    esp_now_send(broadcastMac, (uint8_t*)&pkt, sizeof(Packet));
}

// ─────────────────────────────────────────────────────
//  ТАЙМЕРЫ
// ─────────────────────────────────────────────────────
uint32_t lastDmgMs  = 0;
uint32_t lastRadMs  = 0;
uint32_t lastHealMs = 0;
uint32_t lastProtMs = 0;
int      protIdx    = 0;

// ─────────────────────────────────────────────────────
//  РЕЖИМ АНОМАЛИИ
// ─────────────────────────────────────────────────────
void loopAnomaly() {
    uint32_t now = millis();

    if (cfgFreq > 0 && (now - lastDmgMs >= (uint32_t)cfgFreq * 1000UL)) {
        lastDmgMs = now;
        sendPacket(EMITTER_ANOMALY, MSG_DAMAGE, cfgDmg, 0, cfgDmgMask);
        Serial.println("DMG dmg=" + String(cfgDmg) + " mask=" + String(cfgDmgMask));
    }

    if (cfgRadDmg > 0 && cfgRadFrq > 0 &&
        (now - lastRadMs >= (uint32_t)cfgRadFrq * 1000UL)) {
        lastRadMs = now;
        sendPacket(EMITTER_ANOMALY, MSG_RADIATION, cfgRadDmg, 0, 0);
        Serial.println("RAD rad=" + String(cfgRadDmg));
    }
}

// ─────────────────────────────────────────────────────
//  РЕЖИМ УБЕЖИЩА
// ─────────────────────────────────────────────────────
void loopSafeZone() {
    uint32_t now = millis();

    // Хил-тик (val3=0 → ПДА знает: это хил, не защита)
    if (cfgSzHpFrq > 0 && (now - lastHealMs >= (uint32_t)cfgSzHpFrq * 1000UL)) {
        lastHealMs = now;
        sendPacket(EMITTER_BASE, MSG_SAFE_ZONE, cfgSzHp, cfgSzRad, 0);
        Serial.println("HEAL hp=" + String(cfgSzHp) + " rad=" + String(cfgSzRad));
    }

    // Beacon + защита (каждые 2 сек, 1 тип за раз → все 8 типов за 16 сек)
    // Держит isInSafeZone()=true на ПДА непрерывно
    if (now - lastProtMs >= 2000UL) {
        lastProtMs = now;
        // Beacon всегда несёт флаг защиты от выброса (val1 = cfgSzEmit)
        sendPacket(EMITTER_BASE, MSG_SAFE_ZONE, cfgSzEmit, 0, SZ_BEACON_FLAG);
        if (cfgSzProt[protIdx] > 0) {
            sendPacket(EMITTER_BASE, MSG_SAFE_ZONE,
                       protIdx, cfgSzProt[protIdx], SZ_PROT_FLAG);
        }
        protIdx = (protIdx + 1) % 8;
    }
}

// ─────────────────────────────────────────────────────
//  SETUP / LOOP
// ─────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(300);

    loadConfig();
    initFieldId();

    Serial.println("=== STALKER Field Device v1.0 ===");
    Serial.print("BEACON:id=");
    Serial.println(fieldId);
    printWho();
    Serial.println("INFO: этот id — номер железки для карты мастера");
    Serial.println("MODE: " + String(cfgCat == 0 ? "ANOMALY" : "SAFEZONE"));
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

    Serial.println("READY");
}

void loop() {
    handleSerial();
    if (cfgCat == 0) loopAnomaly();
    else              loopSafeZone();
}
