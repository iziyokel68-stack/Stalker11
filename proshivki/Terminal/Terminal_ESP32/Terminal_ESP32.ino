/**
 * S.T.A.L.K.E.R. — Универсальный терминал (Terminal_ESP32) v1.0
 *
 * ─── Выбор платы (Arduino IDE → Tools → Board) ───
 *
 *   • ESP32 Dev Module — ESP32-WROOM-32 DevKit (рекомендуется)
 *       I2C: GPIO 21 (SDA), GPIO 22 (SCL)
 *       Serial: USB-UART (CP2102 / CH340), 115200 бод
 *
 *   • ESP32C3 Dev Module — ESP32-C3 SuperMini
 *       I2C: GPIO 5 (SDA), GPIO 6 (SCL)
 *
 * Принудительный профиль (раскомментируйте ОДИН):
 *   // #define TERMINAL_BOARD_WROOM32
 *   // #define TERMINAL_BOARD_ESP32C3
 *
 * Роли терминала (NVS key "terminal_role", переживает перезагрузку):
 *   STORE  — касса (op=PURCHASE, 0)
 *   ATM    — банкомат (op=ATM, 4)
 *   QUEST  — квестовая доска (op=QUEST, 2)
 *   ADMIT  — допуск в игру (op=ADMIT, 3)
 *   BANK   — банк (op=BANK, 1)
 *
 * Протокол Serial (115200):
 *   При загрузке: TERMINAL_ROLE:... и STALKER:TERMINAL:v1,... (до READY)
 *   STALKER_WHO              → STALKER:TERMINAL:v1,role=STORE,op_default=0,name=Касса
 *   TERMINAL_ROLE            → TERMINAL_ROLE:STORE,op_default=0,name=Касса
 *   TERMINAL_ROLE:ATM        → OK:TERMINAL_ROLE:ATM
 *   PING                     → PONG
 *   I2C_SCAN / EEPROM_PING
 *   TXN_START:amount=500,item=1[,op=N]  — без op используется op роли
 *   TXN_STATUS / TXN_WAIT / TXN_RESET
 *
 * Доска заданий (роль QUEST), каталог в EEPROM кассеты:
 *   QUEST_CATALOG_CLEAR / QUEST_ADD:... / QUEST_CATALOG_COMMIT
 *   QUEST_CLAIMS / QUEST_CLAIMS_CLEAR / QUEST_DUMP
 *   ПДА пишет заявку TQ @0xA4; терминал отвечает QUEST TXN @0x80
 *
 * Тест: test_firmware/Terminal_Test/README_Terminal_Test.md
 */

#include <Wire.h>
#include <Preferences.h>
#include "eeprom_protocol.h"
#include "mux_channels.h"
#include "quest_catalog.h"

// ─── Профиль платы ───
// #define TERMINAL_BOARD_WROOM32
// #define TERMINAL_BOARD_ESP32C3

#if defined(TERMINAL_BOARD_ESP32C3)
  #define BOARD_NAME "ESP32-C3"
  #define I2C_SDA    5
  #define I2C_SCL    6
#elif defined(TERMINAL_BOARD_WROOM32)
  #define BOARD_NAME "ESP32-WROOM32"
  #define I2C_SDA    21
  #define I2C_SCL    22
#elif defined(CONFIG_IDF_TARGET_ESP32C3) || defined(ARDUINO_ESP32C3_DEV)
  #define BOARD_NAME "ESP32-C3"
  #define I2C_SDA    5
  #define I2C_SCL    6
#elif defined(CONFIG_IDF_TARGET_ESP32) || defined(ARDUINO_ESP32_DEV)
  #define BOARD_NAME "ESP32-WROOM32"
  #define I2C_SDA    21
  #define I2C_SCL    22
#else
  #error "Выберите плату: ESP32 Dev Module или ESP32C3 Dev Module (или задайте TERMINAL_BOARD_*)"
#endif

#define USE_TCA_MUX 1
#define TERMINAL_TEST_MODE 0

#define EEPROM_ADDR EEPROM_ADDR_DEFAULT
#define EEPROM_WR_DLY 5
#define NVS_NS "stalker"
#define NVS_KEY_ROLE "terminal_role"
#define NVS_KEY_CLAIMS "qclm"
#define QUEST_POLL_MS 200
#define CLAIM_REC_SIZE 28

enum TerminalRole : uint8_t {
    ROLE_STORE = 0,
    ROLE_ATM,
    ROLE_QUEST,
    ROLE_ADMIT,
    ROLE_BANK,
    ROLE_COUNT
};

static const char *ROLE_NAMES[] = {"STORE", "ATM", "QUEST", "ADMIT", "BANK"};
static const char *ROLE_DISPLAY[] = {
    "Касса", "Банкомат", "Квестовая доска", "Допуск", "Банк"
};
static const uint8_t ROLE_DEFAULT_OP[] = {
    TXN_OP_PURCHASE,
    TXN_OP_ATM,
    TXN_OP_QUEST,
    TXN_OP_ADMIT,
    TXN_OP_BANK
};

Preferences prefs;
TerminalRole currentRole = ROLE_STORE;

#if USE_TCA_MUX
#define CASSETTE_CHANNEL MUX_CH_UNIVERSAL

bool tcaSelect(uint8_t channel) {
    if (channel > 7) return false;
    Wire.beginTransmission(TCA_ADDR_DEFAULT);
    Wire.write((uint8_t)(1 << channel));
    return Wire.endTransmission() == 0;
}

void muxSelectCassette() {
    tcaSelect(CASSETTE_CHANNEL);
}
#else
void muxSelectCassette() {}
#endif

String serialBuf = "";
uint32_t nextTxnId = 1;

bool parseKeyVal(const String &cfg, const char *key, int &out);
bool parseKeyStr(const String &cfg, const char *key, String &out);

QuestCatRec questCards[QUEST_CAT_MAX];
uint8_t questCardCount = 0;

struct QuestClaimRec {
    char id[9];
    char uid[13];
    uint8_t status;
    uint8_t mode;
    uint16_t timeout_min;
    uint16_t held_at_min;
};
QuestClaimRec questClaims[QUEST_CAT_MAX];
uint8_t questClaimCount = 0;

bool questTxnFromTake = false;
char questPendingId[9] = "";
char questPendingUid[13] = "";
uint8_t questPendingComplete = 0;
uint32_t lastQuestPollMs = 0;

uint16_t nowMin() {
    return (uint16_t)(millis() / 60000UL);
}

const char *questModeName(uint8_t mode) {
    if (mode == QUEST_MODE_ONESHOT) return "oneshot";
    if (mode == QUEST_MODE_SHARED) return "shared";
    return "timeout";
}

const char *questClaimName(uint8_t st) {
    switch (st) {
    case QUEST_CLAIM_HELD: return "HELD";
    case QUEST_CLAIM_DONE: return "DONE";
    case QUEST_CLAIM_GONE: return "GONE";
    default: return "FREE";
    }
}

int findQuestCard(const char *qid) {
    if (!qid || !qid[0]) return -1;
    for (uint8_t i = 0; i < questCardCount; i++) {
        if (strncmp(questCards[i].id, qid, 8) == 0)
            return (int)i;
    }
    return -1;
}

int findQuestClaim(const char *qid) {
    if (!qid || !qid[0]) return -1;
    for (uint8_t i = 0; i < questClaimCount; i++) {
        if (strncmp(questClaims[i].id, qid, 8) == 0)
            return (int)i;
    }
    return -1;
}

int ensureQuestClaim(const QuestCatRec *card) {
    int idx = findQuestClaim(card->id);
    if (idx >= 0) {
        questClaims[idx].mode = card->mode;
        questClaims[idx].timeout_min = card->timeout_min;
        return idx;
    }
    if (questClaimCount >= QUEST_CAT_MAX) return -1;
    idx = questClaimCount++;
    memset(&questClaims[idx], 0, sizeof(questClaims[idx]));
    strncpy(questClaims[idx].id, card->id, 8);
    questClaims[idx].id[8] = '\0';
    questClaims[idx].status = QUEST_CLAIM_FREE;
    questClaims[idx].mode = card->mode;
    questClaims[idx].timeout_min = card->timeout_min;
    return idx;
}

uint16_t claimElapsed(const QuestClaimRec &c) {
    if (c.status != QUEST_CLAIM_HELD)
        return 0;
    return (uint16_t)(nowMin() - c.held_at_min);
}

int questIsAvailable(const char *qid) {
    int ci = findQuestCard(qid);
    if (ci < 0) return 0;
    int hi = findQuestClaim(qid);
    uint8_t st = (hi >= 0) ? questClaims[hi].status : QUEST_CLAIM_FREE;
    uint16_t elapsed = (hi >= 0) ? claimElapsed(questClaims[hi]) : 0;
    uint16_t tmin = questCards[ci].timeout_min;
    return quest_is_listed(questCards[ci].mode, st, elapsed, tmin);
}

int roleFromName(const String &name) {
    String u = name;
    u.toUpperCase();
    u.trim();
    for (uint8_t i = 0; i < ROLE_COUNT; i++) {
        if (u == ROLE_NAMES[i]) return (int)i;
    }
    return -1;
}

void loadTerminalRole() {
    prefs.begin(NVS_NS, true);
    uint8_t stored = prefs.getUChar(NVS_KEY_ROLE, ROLE_STORE);
    prefs.end();
    if (stored >= ROLE_COUNT) stored = ROLE_STORE;
    currentRole = (TerminalRole)stored;
}

void saveTerminalRole(TerminalRole role) {
    currentRole = role;
    prefs.begin(NVS_NS, false);
    prefs.putUChar(NVS_KEY_ROLE, (uint8_t)role);
    prefs.end();
}

String terminalRoleLine() {
    uint8_t r = (uint8_t)currentRole;
    String s = "TERMINAL_ROLE:" + String(ROLE_NAMES[r]);
    s += ",op_default=" + String(ROLE_DEFAULT_OP[r]);
    s += ",name=" + String(ROLE_DISPLAY[r]);
    return s;
}

String stalkerWhoLine() {
    uint8_t r = (uint8_t)currentRole;
    String s = "STALKER:TERMINAL:v1,role=" + String(ROLE_NAMES[r]);
    s += ",op_default=" + String(ROLE_DEFAULT_OP[r]);
    s += ",name=" + String(ROLE_DISPLAY[r]);
    return s;
}

bool eepromWriteByte(uint16_t addr, uint8_t val) {
    muxSelectCassette();
    Wire.beginTransmission(EEPROM_ADDR);
    Wire.write((uint8_t)(addr >> 8));
    Wire.write((uint8_t)(addr & 0xFF));
    Wire.write(val);
    uint8_t err = Wire.endTransmission();
    delay(EEPROM_WR_DLY);
    return err == 0;
}

bool eepromReadByte(uint16_t addr, uint8_t &val) {
    muxSelectCassette();
    Wire.beginTransmission(EEPROM_ADDR);
    Wire.write((uint8_t)(addr >> 8));
    Wire.write((uint8_t)(addr & 0xFF));
    if (Wire.endTransmission() != 0) return false;
    Wire.requestFrom((uint8_t)EEPROM_ADDR, (uint8_t)1);
    if (!Wire.available()) return false;
    val = Wire.read();
    return true;
}

bool eepromWriteBlock(uint16_t addr, const uint8_t *data, uint8_t len) {
    for (uint8_t i = 0; i < len; i++) {
        if (!eepromWriteByte(addr + i, data[i])) return false;
    }
    return true;
}

bool eepromReadBlock(uint16_t addr, uint8_t *buf, uint8_t len) {
    for (uint8_t i = 0; i < len; i++) {
        if (!eepromReadByte(addr + i, buf[i])) return false;
    }
    return true;
}

bool eepromWriteBlockFast(uint16_t addr, const uint8_t *data, uint16_t len) {
    while (len) {
        uint8_t pageLeft = (uint8_t)(64 - (addr & 63));
        uint8_t chunk = (len < pageLeft) ? (uint8_t)len : pageLeft;
        if (chunk > 16) chunk = 16;
        muxSelectCassette();
        Wire.beginTransmission(EEPROM_ADDR);
        Wire.write((uint8_t)(addr >> 8));
        Wire.write((uint8_t)(addr & 0xFF));
        for (uint8_t i = 0; i < chunk; i++)
            Wire.write(data[i]);
        if (Wire.endTransmission() != 0)
            return false;
        delay(EEPROM_WR_DLY);
        addr += chunk;
        data += chunk;
        len -= chunk;
    }
    return true;
}

bool eepromReadBlockFast(uint16_t addr, uint8_t *buf, uint16_t len) {
    while (len) {
        uint8_t chunk = (len > 16) ? 16 : (uint8_t)len;
        muxSelectCassette();
        Wire.beginTransmission(EEPROM_ADDR);
        Wire.write((uint8_t)(addr >> 8));
        Wire.write((uint8_t)(addr & 0xFF));
        if (Wire.endTransmission() != 0)
            return false;
        Wire.requestFrom((uint8_t)EEPROM_ADDR, chunk);
        for (uint8_t i = 0; i < chunk; i++) {
            if (!Wire.available())
                return false;
            buf[i] = Wire.read();
        }
        addr += chunk;
        buf += chunk;
        len -= chunk;
    }
    return true;
}

bool packCatalogBlob(uint8_t *dst, uint16_t *outLen, const QuestCatRec *cards, uint8_t count) {
    if (count > QUEST_CAT_MAX)
        count = QUEST_CAT_MAX;
    uint8_t recs[QUEST_CAT_MAX * QUEST_CAT_REC_SIZE];
    memset(recs, 0, sizeof(recs));
    for (uint8_t i = 0; i < count; i++)
        quest_cat_pack_rec(recs + i * QUEST_CAT_REC_SIZE, &cards[i]);
    uint16_t crc = quest_crc16(recs, (size_t)count * QUEST_CAT_REC_SIZE);
    quest_cat_pack_hdr(dst, count, crc);
    memcpy(dst + QUEST_CAT_HDR_SIZE, recs, (size_t)count * QUEST_CAT_REC_SIZE);
    if (outLen)
        *outLen = (uint16_t)(QUEST_CAT_HDR_SIZE + count * QUEST_CAT_REC_SIZE);
    return true;
}

bool unpackCatalogBlob(const uint8_t *src, uint16_t len, QuestCatRec *cards, uint8_t *count) {
    if (len < QUEST_CAT_HDR_SIZE)
        return false;
    uint8_t n = 0;
    uint16_t crc = 0;
    if (!quest_cat_parse_hdr(src, &n, &crc))
        return false;
    if (n > QUEST_CAT_MAX)
        n = QUEST_CAT_MAX;
    uint16_t recBytes = (uint16_t)n * QUEST_CAT_REC_SIZE;
    if (len < QUEST_CAT_HDR_SIZE + recBytes)
        return false;
    if (quest_crc16(src + QUEST_CAT_HDR_SIZE, recBytes) != crc)
        return false;
    for (uint8_t i = 0; i < n; i++)
        quest_cat_parse_rec(src + QUEST_CAT_HDR_SIZE + i * QUEST_CAT_REC_SIZE, &cards[i]);
    if (count)
        *count = n;
    return true;
}

void saveClaimsNvs() {
    uint8_t blob[QUEST_CAT_MAX * CLAIM_REC_SIZE];
    memset(blob, 0, sizeof(blob));
    for (uint8_t i = 0; i < questClaimCount; i++) {
        uint8_t *p = blob + i * CLAIM_REC_SIZE;
        memcpy(p, questClaims[i].id, 8);
        memcpy(p + 8, questClaims[i].uid, 12);
        p[20] = questClaims[i].status;
        p[21] = questClaims[i].mode;
        quest_put_u16(p + 22, questClaims[i].timeout_min);
        quest_put_u16(p + 24, questClaims[i].held_at_min);
    }
    prefs.begin(NVS_NS, false);
    prefs.putUChar("qcln", questClaimCount);
    prefs.putBytes(NVS_KEY_CLAIMS, blob, questClaimCount * CLAIM_REC_SIZE);
    prefs.end();
}

void loadClaimsNvs() {
    questClaimCount = 0;
    memset(questClaims, 0, sizeof(questClaims));
    prefs.begin(NVS_NS, true);
    uint8_t n = prefs.getUChar("qcln", 0);
    uint8_t blob[QUEST_CAT_MAX * CLAIM_REC_SIZE];
    size_t got = prefs.getBytes(NVS_KEY_CLAIMS, blob, sizeof(blob));
    prefs.end();
    if (n > QUEST_CAT_MAX)
        n = QUEST_CAT_MAX;
    if (got < (size_t)n * CLAIM_REC_SIZE)
        n = (uint8_t)(got / CLAIM_REC_SIZE);
    uint16_t bootMin = nowMin();
    for (uint8_t i = 0; i < n; i++) {
        uint8_t *p = blob + i * CLAIM_REC_SIZE;
        memcpy(questClaims[i].id, p, 8);
        questClaims[i].id[8] = '\0';
        memcpy(questClaims[i].uid, p + 8, 12);
        questClaims[i].uid[12] = '\0';
        questClaims[i].status = p[20];
        questClaims[i].mode = p[21];
        questClaims[i].timeout_min = quest_u16(p + 22);
        questClaims[i].held_at_min = quest_u16(p + 24);
        if (questClaims[i].status == QUEST_CLAIM_HELD)
            questClaims[i].held_at_min = bootMin;
        questClaimCount++;
    }
}

bool writeCatalogEeprom(uint16_t base, const QuestCatRec *cards, uint8_t count) {
    uint8_t blob[QUEST_CAT_HDR_SIZE + QUEST_CAT_MAX * QUEST_CAT_REC_SIZE];
    uint16_t len = 0;
    packCatalogBlob(blob, &len, cards, count);
    return eepromWriteBlockFast(base, blob, len);
}

bool readCatalogEeprom(uint16_t base, QuestCatRec *cards, uint8_t *count) {
    uint8_t hdr[QUEST_CAT_HDR_SIZE];
    if (!eepromReadBlockFast(base, hdr, QUEST_CAT_HDR_SIZE))
        return false;
    uint8_t n = 0;
    uint16_t crc = 0;
    if (!quest_cat_parse_hdr(hdr, &n, &crc))
        return false;
    if (n > QUEST_CAT_MAX)
        n = QUEST_CAT_MAX;
    uint8_t blob[QUEST_CAT_HDR_SIZE + QUEST_CAT_MAX * QUEST_CAT_REC_SIZE];
    memcpy(blob, hdr, QUEST_CAT_HDR_SIZE);
    uint16_t recBytes = (uint16_t)n * QUEST_CAT_REC_SIZE;
    if (recBytes && !eepromReadBlockFast(base + QUEST_CAT_HDR_SIZE,
                                         blob + QUEST_CAT_HDR_SIZE, recBytes))
        return false;
    return unpackCatalogBlob(blob, (uint16_t)(QUEST_CAT_HDR_SIZE + recBytes),
                             cards, count);
}

void pruneClaimsToCatalog() {
    uint8_t w = 0;
    for (uint8_t i = 0; i < questClaimCount; i++) {
        if (findQuestCard(questClaims[i].id) >= 0)
            questClaims[w++] = questClaims[i];
    }
    questClaimCount = w;
}

bool writeListedCatalog() {
    QuestCatRec listed[QUEST_CAT_MAX];
    uint8_t n = 0;
    for (uint8_t i = 0; i < questCardCount && n < QUEST_CAT_MAX; i++) {
        int hi = findQuestClaim(questCards[i].id);
        uint8_t st = (hi >= 0) ? questClaims[hi].status : QUEST_CLAIM_FREE;
        uint16_t elapsed = (hi >= 0) ? claimElapsed(questClaims[hi]) : 0;
        if (quest_is_listed(questCards[i].mode, st, elapsed, questCards[i].timeout_min))
            listed[n++] = questCards[i];
    }
    return writeCatalogEeprom(QUEST_CAT_BASE, listed, n);
}

bool expireQuestHolds() {
    bool changed = false;
    for (uint8_t i = 0; i < questClaimCount; i++) {
        if (questClaims[i].status != QUEST_CLAIM_HELD)
            continue;
        uint8_t next = quest_next_claim(questClaims[i].mode, questClaims[i].status,
                                        claimElapsed(questClaims[i]),
                                        questClaims[i].timeout_min, 0);
        if (next != questClaims[i].status) {
            questClaims[i].status = next;
            if (next == QUEST_CLAIM_FREE) {
                memset(questClaims[i].uid, 0, sizeof(questClaims[i].uid));
                questClaims[i].held_at_min = 0;
            }
            changed = true;
        }
    }
    if (changed)
        saveClaimsNvs();
    return changed;
}

void clearTakeSlot() {
    uint8_t z[QUEST_TAKE_SIZE];
    memset(z, 0, sizeof(z));
    eepromWriteBlockFast(QUEST_TAKE_BASE, z, QUEST_TAKE_SIZE);
}

bool readTakeSlot(char *qid, char *uid, uint8_t *flags) {
    uint8_t buf[QUEST_TAKE_SIZE];
    if (!eepromReadBlockFast(QUEST_TAKE_BASE, buf, QUEST_TAKE_SIZE))
        return false;
    return quest_take_parse(buf, qid, uid, flags) != 0;
}

bool startQuestTxn(const QuestCatRec *card, bool complete, uint16_t itemId) {
    uint8_t block[TXN_BLOCK_SIZE];
    if (!txnRead(block))
        return false;
    if (txn_validate_block(block) && txn_get_state(block) != TXN_STATE_IDLE)
        return false;
    uint32_t txnId = nextTxnId++;
    int32_t amount = complete ? card->rub : 0;
    txn_build_quest(block, txnId, itemId, amount, card->id, complete,
                    card->hidden != 0);
    txn_set_state(block, TXN_STATE_PENDING);
    if (!txnWrite(block))
        return false;
    return true;
}

void applyQuestTakeSuccess() {
    int ci = findQuestCard(questPendingId);
    if (ci < 0)
        return;
    int hi = ensureQuestClaim(&questCards[ci]);
    if (hi < 0)
        return;
    if (questPendingComplete) {
        uint8_t mode = questClaims[hi].mode;
        bool holder = (questClaims[hi].uid[0] == 0) ||
                      (strncmp(questClaims[hi].uid, questPendingUid, 12) == 0);
        if (mode == QUEST_MODE_SHARED) {
            questClaims[hi].status = QUEST_CLAIM_FREE;
        } else if (holder || questClaims[hi].status == QUEST_CLAIM_FREE) {
            questClaims[hi].status = QUEST_CLAIM_DONE;
        }
    } else if (questCards[ci].mode != QUEST_MODE_SHARED) {
        questClaims[hi].status = QUEST_CLAIM_HELD;
        strncpy(questClaims[hi].uid, questPendingUid, 12);
        questClaims[hi].uid[12] = '\0';
        questClaims[hi].held_at_min = nowMin();
        questClaims[hi].mode = questCards[ci].mode;
        questClaims[hi].timeout_min = questCards[ci].timeout_min;
    }
    saveClaimsNvs();
    writeListedCatalog();
}

void processTakeRequest() {
    char qid[9], uid[13];
    uint8_t flags = 0;
    if (!readTakeSlot(qid, uid, &flags))
        return;
    clearTakeSlot();
    int ci = findQuestCard(qid);
    if (ci < 0)
        return;
    bool complete = (flags & QUEST_TAKE_COMPLETE) != 0;
    if (!complete && !questIsAvailable(qid))
        return;
    strncpy(questPendingId, qid, 8);
    questPendingId[8] = '\0';
    strncpy(questPendingUid, uid, 12);
    questPendingUid[12] = '\0';
    questPendingComplete = complete ? 1 : 0;
    questTxnFromTake = true;
    if (!startQuestTxn(&questCards[ci], complete, (uint16_t)(ci + 1))) {
        questTxnFromTake = false;
        questPendingId[0] = '\0';
    }
}

void questBoardTick() {
    if (!eepromPresent())
        return;
    bool expired = expireQuestHolds();
    if (expired)
        writeListedCatalog();

    uint8_t block[TXN_BLOCK_SIZE];
    if (!txnRead(block) || !txn_validate_block(block))
        return;
    uint8_t st = txn_get_state(block);
    if (questTxnFromTake && txn_is_terminal_state(st)) {
        if (st == TXN_STATE_SUCCESS)
            applyQuestTakeSuccess();
        questTxnFromTake = false;
        questPendingId[0] = '\0';
        txn_init_idle(block);
        txnWrite(block);
        return;
    }
    if (st == TXN_STATE_IDLE)
        processTakeRequest();
}

void loadQuestBoard() {
    questCardCount = 0;
    memset(questCards, 0, sizeof(questCards));
    loadClaimsNvs();
    if (eepromPresent()) {
        if (!readCatalogEeprom(QUEST_CAT_FULL_BASE, questCards, &questCardCount))
            readCatalogEeprom(QUEST_CAT_BASE, questCards, &questCardCount);
        pruneClaimsToCatalog();
        writeListedCatalog();
        uint8_t block[TXN_BLOCK_SIZE];
        if (txnRead(block) && txn_validate_block(block) &&
            txn_is_terminal_state(txn_get_state(block))) {
            txn_init_idle(block);
            txnWrite(block);
        }
        clearTakeSlot();
    }
}

bool parseKeyStrRest(const String &cfg, const char *key, String &out) {
    String needle = String(key) + "=";
    int pos = cfg.indexOf(needle);
    if (pos < 0) return false;
    out = cfg.substring(pos + needle.length());
    out.trim();
    return true;
}

void cmdQuestCatalogClear() {
    questCardCount = 0;
    memset(questCards, 0, sizeof(questCards));
    Serial.println("OK:QUEST_CATALOG_CLEAR");
}

void cmdQuestAdd(const String &args) {
    if (questCardCount >= QUEST_CAT_MAX) {
        Serial.println("ERROR:QUEST_CATALOG_FULL");
        return;
    }
    String id, title, modeStr;
    int rub = 0, hidden = 0, tmin = QUEST_TIMEOUT_DEFAULT;
    parseKeyStr(args, "id", id);
    parseKeyVal(args, "rub", rub);
    parseKeyVal(args, "hidden", hidden);
    parseKeyVal(args, "timeout_min", tmin);
    parseKeyStr(args, "mode", modeStr);
    parseKeyStrRest(args, "title", title);
    id.trim();
    if (id.length() == 0 || title.length() == 0) {
        Serial.println("ERROR:BAD_QUEST");
        return;
    }
    if (tmin <= 0)
        tmin = QUEST_TIMEOUT_DEFAULT;
    QuestCatRec q;
    memset(&q, 0, sizeof(q));
    strncpy(q.id, id.c_str(), 8);
    q.id[8] = '\0';
    strncpy(q.title, title.c_str(), 24);
    q.title[24] = '\0';
    q.rub = rub;
    q.hidden = hidden ? 1 : 0;
    q.mode = quest_mode_from_name(modeStr.c_str());
    q.timeout_min = (uint16_t)tmin;
    int exist = findQuestCard(q.id);
    if (exist >= 0)
        questCards[exist] = q;
    else
        questCards[questCardCount++] = q;
    Serial.printf("OK:QUEST_ADD:%s\n", q.id);
}

void cmdQuestCatalogCommit() {
    if (!eepromPresent()) {
        Serial.println("ERROR:EEPROM_NOT_FOUND");
        return;
    }
    pruneClaimsToCatalog();
    if (!writeCatalogEeprom(QUEST_CAT_FULL_BASE, questCards, questCardCount)) {
        Serial.println("ERROR:WRITE_FAILED");
        return;
    }
    if (!writeListedCatalog()) {
        Serial.println("ERROR:WRITE_FAILED");
        return;
    }
    saveClaimsNvs();
    Serial.printf("OK:QUEST_CATALOG_COMMIT:count=%u\n", (unsigned)questCardCount);
}

void cmdQuestDump() {
    Serial.printf("QUEST_DUMP:count=%u\n", (unsigned)questCardCount);
    for (uint8_t i = 0; i < questCardCount; i++) {
        Serial.printf("QUEST:%s,rub=%ld,mode=%s,timeout_min=%u,hidden=%u,title=%s\n",
                      questCards[i].id, (long)questCards[i].rub,
                      questModeName(questCards[i].mode),
                      (unsigned)questCards[i].timeout_min,
                      (unsigned)questCards[i].hidden, questCards[i].title);
    }
}

void cmdQuestClaims() {
    Serial.printf("QUEST_CLAIMS:count=%u\n", (unsigned)questClaimCount);
    for (uint8_t i = 0; i < questClaimCount; i++) {
        Serial.printf("CLAIM:%s,uid=%s,status=%s,mode=%s,timeout_min=%u,held_min=%u,elapsed=%u\n",
                      questClaims[i].id, questClaims[i].uid[0] ? questClaims[i].uid : "-",
                      questClaimName(questClaims[i].status),
                      questModeName(questClaims[i].mode),
                      (unsigned)questClaims[i].timeout_min,
                      (unsigned)questClaims[i].held_at_min,
                      (unsigned)claimElapsed(questClaims[i]));
    }
}

void cmdQuestClaimsClear() {
    questClaimCount = 0;
    memset(questClaims, 0, sizeof(questClaims));
    saveClaimsNvs();
    writeListedCatalog();
    Serial.println("OK:QUEST_CLAIMS_CLEAR");
}

bool eepromPresent() {
    muxSelectCassette();
    Wire.beginTransmission(EEPROM_ADDR);
    return Wire.endTransmission() == 0;
}

bool txnRead(uint8_t *block) {
    return eepromReadBlock(TXN_EEPROM_BASE, block, TXN_BLOCK_SIZE);
}

bool txnWrite(const uint8_t *block) {
    return eepromWriteBlock(TXN_EEPROM_BASE, block, TXN_BLOCK_SIZE);
}

String txnStatusLine(const uint8_t *block) {
    uint16_t result = txn_read_u16(block, TXN_OFF_RESULT);
    uint8_t op = block[TXN_OFF_OP_TYPE];
    String s = "TXN:state=" + String(block[TXN_OFF_STATE]);
    s += ",txn_id=" + String(txn_read_u32(block, TXN_OFF_TXN_ID));
    s += ",amount=" + String(txn_read_i32(block, TXN_OFF_AMOUNT));
    s += ",item=" + String(txn_read_u16(block, TXN_OFF_ITEM_ID));
    s += ",paid=" + String(txn_read_i32(block, TXN_OFF_PAID));
    s += ",balance=" + String(txn_read_i32(block, TXN_OFF_BALANCE));
    s += ",result=" + String(result);
    s += ",result_name=" + String(txn_result_name(result));
    s += ",op=" + String(op);
    s += ",op_name=" + String(txn_op_name(op));
    s += ",flags=" + String(block[TXN_OFF_FLAGS]);
    return s;
}

bool parseKeyVal(const String &cfg, const char *key, int &out) {
    String needle = String(key) + "=";
    int pos = cfg.indexOf(needle);
    if (pos < 0) return false;
    int start = pos + needle.length();
    int end = cfg.indexOf(',', start);
    if (end < 0) end = cfg.length();
    out = cfg.substring(start, end).toInt();
    return true;
}

bool parseKeyStr(const String &cfg, const char *key, String &out) {
    String needle = String(key) + "=";
    int pos = cfg.indexOf(needle);
    if (pos < 0) return false;
    int start = pos + needle.length();
    int end = cfg.indexOf(',', start);
    if (end < 0) end = cfg.length();
    out = cfg.substring(start, end);
    out.trim();
    return true;
}

void cmdI2cScan() {
    Serial.println("I2C_SCAN:begin");
    uint8_t found = 0;
    for (uint8_t addr = 0x03; addr < 0x78; addr++) {
        muxSelectCassette();
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  0x%02X\n", addr);
            found++;
        }
        delay(2);
    }
#if USE_TCA_MUX
    Serial.printf("I2C_SCAN:end count=%u (TCA CH%u selected)\n",
                  (unsigned)found, (unsigned)CASSETTE_CHANNEL);
#else
    Serial.printf("I2C_SCAN:end count=%u (direct bus)\n", (unsigned)found);
#endif
}

void cmdEepromPing() {
    if (!eepromPresent()) {
        Serial.println("EEPROM:MISSING");
        return;
    }
    Serial.println("EEPROM:OK addr=0x50");
    uint8_t block[TXN_BLOCK_SIZE];
    if (!txnRead(block)) {
        Serial.println("EEPROM:READ_FAIL");
        return;
    }
    if (!txn_validate_block(block)) {
        Serial.println("EEPROM:TXN_BAD_CRC");
        return;
    }
    Serial.println("EEPROM:TXN_OK " + txnStatusLine(block));
}

void cmdTerminalRole(const String &args) {
    if (args.length() == 0) {
        Serial.println(terminalRoleLine());
        return;
    }
    int idx = roleFromName(args);
    if (idx < 0) {
        Serial.println("ERROR:BAD_ROLE");
        return;
    }
    saveTerminalRole((TerminalRole)idx);
    Serial.println("OK:" + terminalRoleLine());
}

void cmdTxnStart(const String &args) {
    if (!eepromPresent()) {
        Serial.println("ERROR:EEPROM_NOT_FOUND");
        return;
    }
    int amount = 0, item = 0, txnId = 0, op = ROLE_DEFAULT_OP[(uint8_t)currentRole], flags = 0;
    bool opSpecified = false;
    String questId;
    parseKeyVal(args, "amount", amount);
    parseKeyVal(args, "item", item);
    if (parseKeyVal(args, "op", op)) opSpecified = true;
    if (!opSpecified) op = ROLE_DEFAULT_OP[(uint8_t)currentRole];
    parseKeyVal(args, "flags", flags);
    parseKeyStr(args, "quest_id", questId);
    if (!parseKeyVal(args, "txn_id", txnId) || txnId <= 0)
        txnId = (int)nextTxnId++;

    uint8_t block[TXN_BLOCK_SIZE];
    if (!txnRead(block)) {
        Serial.println("ERROR:READ_FAILED");
        return;
    }
    if (txn_validate_block(block) && txn_get_state(block) != TXN_STATE_IDLE) {
        Serial.println("ERROR:TXN_BUSY:" + txnStatusLine(block));
        return;
    }

    switch (op) {
    case TXN_OP_PURCHASE:
        if (amount <= 0) { Serial.println("ERROR:BAD_AMOUNT"); return; }
        txn_build_purchase(block, (uint32_t)txnId, amount, (uint16_t)item);
        break;
    case TXN_OP_BANK:
    case TXN_OP_ATM:
        if (amount <= 0) { Serial.println("ERROR:BAD_AMOUNT"); return; }
        txn_build_bank(block, (uint32_t)txnId, amount, (uint16_t)item,
                       (flags & TXN_FLAG_BANK_DEPOSIT) != 0);
        break;
    case TXN_OP_QUEST:
        if (currentRole == ROLE_QUEST && questCardCount > 0 &&
            !(flags & TXN_FLAG_QUEST_COMPLETE)) {
            if (questId.length() && !questIsAvailable(questId.c_str())) {
                Serial.println("ERROR:QUEST_TAKEN");
                return;
            }
        }
        txn_build_quest(block, (uint32_t)txnId, (uint16_t)item, amount,
                        questId.length() ? questId.c_str() : nullptr,
                        (flags & TXN_FLAG_QUEST_COMPLETE) != 0,
                        (flags & TXN_FLAG_QUEST_HIDDEN) != 0);
        break;
    case TXN_OP_ADMIT:
        txn_build_admit(block, (uint32_t)txnId);
        break;
    default:
        Serial.println("ERROR:BAD_OP");
        return;
    }
    if (flags && op != TXN_OP_BANK && op != TXN_OP_QUEST)
        block[TXN_OFF_FLAGS] = (uint8_t)flags;

    txn_set_state(block, TXN_STATE_PENDING);
    if (!txnWrite(block)) {
        Serial.println("ERROR:WRITE_FAILED");
        return;
    }
    Serial.println("OK:TXN_PENDING:" + txnStatusLine(block));
}

void cmdTxnStatus() {
    if (!eepromPresent()) {
        Serial.println("ERROR:EEPROM_NOT_FOUND");
        return;
    }
    uint8_t block[TXN_BLOCK_SIZE];
    if (!txnRead(block)) {
        Serial.println("ERROR:READ_FAILED");
        return;
    }
    if (!txn_validate_block(block)) {
        Serial.println("ERROR:BAD_CRC");
        return;
    }
    Serial.println(txnStatusLine(block));
}

void cmdTxnWait(const String &args) {
    int timeoutMs = 30000;
    parseKeyVal(args, "timeout_ms", timeoutMs);
    uint32_t start = millis();
    while ((int32_t)(millis() - start) < timeoutMs) {
        uint8_t block[TXN_BLOCK_SIZE];
        if (txnRead(block) && txn_validate_block(block)) {
            uint8_t st = txn_get_state(block);
            if (txn_is_terminal_state(st)) {
                if (st == TXN_STATE_SUCCESS)
                    Serial.println("OK:TXN_DONE:" + txnStatusLine(block));
                else
                    Serial.println("ERROR:TXN_FAILED:" + txnStatusLine(block));
                return;
            }
        }
        delay(100);
        yield();
    }
    Serial.println("ERROR:TIMEOUT");
}

void cmdTxnReset() {
    uint8_t block[TXN_BLOCK_SIZE];
    txn_init_idle(block);
    if (txnWrite(block))
        Serial.println("OK:TXN_IDLE");
    else
        Serial.println("ERROR:RESET_FAILED");
}

void processCommand(const String &cmd) {
    if (cmd == "STALKER_WHO") {
        Serial.println(stalkerWhoLine());
    } else if (cmd == "PING") {
        Serial.println("PONG");
    } else if (cmd == "I2C_SCAN") {
        cmdI2cScan();
    } else if (cmd == "EEPROM_PING") {
        cmdEepromPing();
    } else if (cmd == "TERMINAL_ROLE" || cmd.startsWith("TERMINAL_ROLE:")) {
        String args = (cmd.length() > 14) ? cmd.substring(14) : "";
        cmdTerminalRole(args);
    } else if (cmd.startsWith("TXN_START:")) {
        cmdTxnStart(cmd.substring(10));
    } else if (cmd == "TXN_STATUS") {
        cmdTxnStatus();
    } else if (cmd.startsWith("TXN_WAIT:")) {
        cmdTxnWait(cmd.substring(9));
    } else if (cmd == "TXN_WAIT") {
        cmdTxnWait("");
    } else if (cmd == "TXN_RESET") {
        cmdTxnReset();
    } else if (cmd == "QUEST_CATALOG_CLEAR") {
        cmdQuestCatalogClear();
    } else if (cmd.startsWith("QUEST_ADD:")) {
        cmdQuestAdd(cmd.substring(10));
    } else if (cmd == "QUEST_CATALOG_COMMIT") {
        cmdQuestCatalogCommit();
    } else if (cmd == "QUEST_DUMP") {
        cmdQuestDump();
    } else if (cmd == "QUEST_CLAIMS") {
        cmdQuestClaims();
    } else if (cmd == "QUEST_CLAIMS_CLEAR") {
        cmdQuestClaimsClear();
    } else {
        Serial.println("ERROR:UNKNOWN_CMD");
    }
}

void handleSerial() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            serialBuf.trim();
            if (serialBuf.length() > 0) {
                processCommand(serialBuf);
                serialBuf = "";
            }
        } else {
            serialBuf += c;
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(300);
    loadTerminalRole();
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000);

    Serial.println("=== STALKER Terminal v1.1 ===");
    Serial.println(terminalRoleLine());
    Serial.println(stalkerWhoLine());  // handshake для программатора PC (до READY)
    Serial.printf("Board: %s | I2C SDA=GPIO%d SCL=GPIO%d @100kHz\n",
                  BOARD_NAME, I2C_SDA, I2C_SCL);
#if USE_TCA_MUX
    Wire.beginTransmission(TCA_ADDR_DEFAULT);
    if (Wire.endTransmission() == 0)
        Serial.printf("TCA9548A @0x70 OK, cassette on CH%u\n", (unsigned)CASSETTE_CHANNEL);
    else
        Serial.println("TCA9548A: NOT FOUND");
#endif
    if (eepromPresent())
        Serial.println("EEPROM: FOUND at 0x50");
    else
        Serial.println("EEPROM: NOT FOUND — check wiring");
    loadQuestBoard();
    if (currentRole == ROLE_QUEST)
        Serial.printf("QUEST_CATALOG:count=%u\n", (unsigned)questCardCount);
#if TERMINAL_TEST_MODE
    Serial.println("TEST_MODE: ON — use I2C_SCAN / EEPROM_PING");
#endif
    Serial.println("READY");
}

void loop() {
    handleSerial();
    if (currentRole == ROLE_QUEST && (millis() - lastQuestPollMs) >= QUEST_POLL_MS) {
        lastQuestPollMs = millis();
        questBoardTick();
    }
}
