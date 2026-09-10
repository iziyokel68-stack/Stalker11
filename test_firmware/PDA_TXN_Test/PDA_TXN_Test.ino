/**
 * S.T.A.L.K.E.R. — тест ПДА: опрос TXN на CH0 (касса) + проверка CH2 (слот)
 *
 * Минимальная прошивка БЕЗ дисплея, ESP-NOW, NVS.
 * Повторяет pollEepromTransaction() из PDA_ESP32.ino для стенда с кассой.
 *
 * Board: ESP32S3 Dev Module (плата ПДА) — I2C GPIO 9=SDA, 8=SCL
 *        ESP32 Dev Module (WROOM) — I2C GPIO 21/22 (для отладки без S3)
 * Serial: 115200
 *
 * Открывать: test_firmware/PDA_TXN_Test/PDA_TXN_Test.ino
 *
 * Команды Serial:
 *   HELP              — справка
 *   SCAN              — I2C scan главная шина + CH0, CH2
 *   DUMP              — прочитать TXN-блок @0x80 на CH0
 *   STATUS            — деньги, уровень, XP, last txn
 *   SET_MONEY:1000    — тестовый баланс
 *   SET_LEVEL:2       — уровень (магазин с 2+, банк с 3+)
 *   RESET_TXN         — сброс lastProcessedTxnId
 *
 * Авто: каждые 200 ms опрос CH0 — PURCHASE, BANK, QUEST.
 */

#include <Wire.h>
#include "eeprom_protocol.h"
#include "mux_channels.h"

#if CONFIG_IDF_TARGET_ESP32S3
  #define I2C_SDA 9
  #define I2C_SCL 8
  #define BOARD_LABEL "ESP32-S3"
#else
  #define I2C_SDA 21
  #define I2C_SCL 22
  #define BOARD_LABEL "ESP32-WROOM32"
#endif

#define TCA_ADDR     TCA_ADDR_DEFAULT
#define EEPROM_DEV   EEPROM_ADDR_DEFAULT
#define CH_TXN       MUX_CH_UNIVERSAL
#define CH_ARMOR     MUX_CH_SLOT_1
#define EEPROM_WR_DLY 5
#define TXN_POLL_MS 200

#define LVL_STORE 2
#define LVL_BANK  3
#define ACTIVE_QUESTS_MAX 4

int32_t playerMoney = 1000;
int32_t playerXP = 0;
uint8_t playerLevel = 2;
uint32_t lastProcessedTxnId = 0;
uint32_t lastPollMs = 0;

char activeQuestIds[ACTIVE_QUESTS_MAX][9];
uint8_t activeQuestCount = 0;

bool tcaSelect(uint8_t ch) {
  if (ch > 7) return false;
  Wire.beginTransmission(TCA_ADDR);
  Wire.write((uint8_t)(1 << ch));
  return Wire.endTransmission() == 0;
}

bool eepromPing(uint8_t ch) {
  if (!tcaSelect(ch)) return false;
  Wire.beginTransmission(EEPROM_DEV);
  return Wire.endTransmission() == 0;
}

bool eepromReadByte(uint8_t ch, uint16_t addr, uint8_t &val) {
  if (!tcaSelect(ch)) return false;
  Wire.beginTransmission(EEPROM_DEV);
  Wire.write((uint8_t)(addr >> 8));
  Wire.write((uint8_t)(addr & 0xFF));
  if (Wire.endTransmission() != 0) return false;
  Wire.requestFrom((uint8_t)EEPROM_DEV, (uint8_t)1);
  if (!Wire.available()) return false;
  val = Wire.read();
  return true;
}

bool eepromWriteByte(uint8_t ch, uint16_t addr, uint8_t val) {
  if (!tcaSelect(ch)) return false;
  Wire.beginTransmission(EEPROM_DEV);
  Wire.write((uint8_t)(addr >> 8));
  Wire.write((uint8_t)(addr & 0xFF));
  Wire.write(val);
  uint8_t err = Wire.endTransmission();
  delay(EEPROM_WR_DLY);
  return err == 0;
}

bool eepromReadBlock(uint8_t ch, uint16_t addr, uint8_t *buf, uint8_t len) {
  for (uint8_t i = 0; i < len; i++) {
    if (!eepromReadByte(ch, addr + i, buf[i])) return false;
  }
  return true;
}

bool eepromWriteBlock(uint8_t ch, uint16_t addr, const uint8_t *data, uint8_t len) {
  for (uint8_t i = 0; i < len; i++) {
    if (!eepromWriteByte(ch, addr + i, data[i])) return false;
  }
  return true;
}

void cmdScan() {
  Serial.println("--- I2C SCAN ---");
  Serial.print("Main bus: ");
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("0x%02X ", a);
    }
  }
  Serial.println();
  const uint8_t channels[] = {CH_TXN, CH_ARMOR};
  for (uint8_t i = 0; i < 2; i++) {
    uint8_t ch = channels[i];
    Serial.printf("CH%u EEPROM: %s\n", ch, eepromPing(ch) ? "OK @0x50" : "MISSING");
  }
}

void printTxnBlock(const uint8_t *block) {
  if (!txn_validate_block(block)) {
    Serial.println("TXN: invalid CRC/magic");
    return;
  }
  Serial.printf("TXN state=%s id=%lu amount=%ld item=%u op=%s flags=0x%02X result=%u paid=%ld bal=%ld\n",
                txn_state_name(txn_get_state(block)),
                (unsigned long)txn_read_u32(block, TXN_OFF_TXN_ID),
                (long)txn_read_i32(block, TXN_OFF_AMOUNT),
                (unsigned)txn_read_u16(block, TXN_OFF_ITEM_ID),
                txn_op_name(block[TXN_OFF_OP_TYPE]),
                (unsigned)block[TXN_OFF_FLAGS],
                (unsigned)txn_read_u16(block, TXN_OFF_RESULT),
                (long)txn_read_i32(block, TXN_OFF_PAID),
                (long)txn_read_i32(block, TXN_OFF_BALANCE));
}

void cmdDump() {
  uint8_t block[TXN_BLOCK_SIZE];
  if (!eepromReadBlock(CH_TXN, TXN_EEPROM_BASE, block, TXN_BLOCK_SIZE)) {
    Serial.println("DUMP: read failed");
    return;
  }
  printTxnBlock(block);
}

void cmdStatus() {
  Serial.printf("Player: level=%u xp=%ld money=%ld RUB | quests=%u | last_txn_id=%lu\n",
                (unsigned)playerLevel, (long)playerXP, (long)playerMoney,
                (unsigned)activeQuestCount, (unsigned long)lastProcessedTxnId);
}

bool finalizeTxn(uint32_t txnId, uint8_t *block, uint16_t result, int32_t paid) {
  txn_write_i32(block, TXN_OFF_BALANCE, playerMoney);
  txn_write_u16(block, TXN_OFF_RESULT, result);
  txn_write_i32(block, TXN_OFF_PAID, paid);
  block[TXN_OFF_STATE] = (result == TXN_RESULT_OK) ? TXN_STATE_SUCCESS : TXN_STATE_FAILED;
  txn_recalc_crc(block);
  if (!eepromWriteBlock(CH_TXN, TXN_EEPROM_BASE, block, TXN_BLOCK_SIZE))
    return false;
  lastProcessedTxnId = txnId;
  return true;
}

int findActiveQuest(const char *qid) {
  for (uint8_t i = 0; i < activeQuestCount; i++) {
    if (strncmp(activeQuestIds[i], qid, 8) == 0)
      return (int)i;
  }
  return -1;
}

void makeQuestId(char *qid, size_t qidLen, const char *prefix, uint16_t questCatId) {
  memset(qid, 0, qidLen);
  if (prefix && prefix[0]) {
    strncpy(qid, prefix, qidLen - 1);
  } else {
    snprintf(qid, qidLen, "q%u", (unsigned)questCatId);
  }
}

bool handlePurchase(int32_t amount, uint16_t itemId, uint16_t &result, int32_t &paid) {
  if (playerLevel < LVL_STORE) {
    result = TXN_RESULT_LEVEL_TOO_LOW;
    Serial.printf(">>> PURCHASE FAIL res=%u (need level %u)\n",
                  (unsigned)result, (unsigned)LVL_STORE);
    return false;
  }
  if (amount <= 0) {
    result = TXN_RESULT_UNKNOWN;
    Serial.println(">>> PURCHASE FAIL res=UNKNOWN (bad amount)");
    return false;
  }
  if (playerMoney < amount) {
    result = TXN_RESULT_INSUFFICIENT_FUNDS;
    Serial.printf(">>> PURCHASE FAIL res=%u amount=%ld balance=%ld\n",
                  (unsigned)result, (long)amount, (long)playerMoney);
    return false;
  }
  paid = amount;
  playerMoney -= paid;
  result = TXN_RESULT_OK;
  Serial.printf(">>> PURCHASE OK item=%u -%ld RUB, balance=%ld\n",
                (unsigned)itemId, (long)paid, (long)playerMoney);
  return true;
}

bool handleBank(int32_t amount, uint8_t flags, uint16_t &result, int32_t &paid) {
  if (playerLevel < LVL_BANK) {
    result = TXN_RESULT_LEVEL_TOO_LOW;
    Serial.printf(">>> BANK FAIL res=%u (need level %u)\n",
                  (unsigned)result, (unsigned)LVL_BANK);
    return false;
  }
  if (amount <= 0) {
    result = TXN_RESULT_UNKNOWN;
    Serial.println(">>> BANK FAIL res=UNKNOWN (bad amount)");
    return false;
  }
  bool deposit = (flags & TXN_FLAG_BANK_DEPOSIT) != 0;
  if (deposit) {
    if (playerMoney < amount) {
      result = TXN_RESULT_INSUFFICIENT_FUNDS;
      Serial.printf(">>> BANK DEPOSIT FAIL res=%u amount=%ld\n",
                    (unsigned)result, (long)amount);
      return false;
    }
    playerMoney -= amount;
    paid = amount;
    result = TXN_RESULT_OK;
    Serial.printf(">>> BANK DEPOSIT OK -%ld RUB, balance=%ld\n",
                  (long)paid, (long)playerMoney);
  } else {
    playerMoney += amount;
    paid = amount;
    result = TXN_RESULT_OK;
    Serial.printf(">>> BANK WITHDRAW OK +%ld RUB, balance=%ld\n",
                  (long)paid, (long)playerMoney);
  }
  return true;
}

bool handleQuest(int32_t rubReward, uint16_t questCatId, uint8_t flags,
                 const char *questPrefix, uint16_t &result, int32_t &paid) {
  char qid[12];
  makeQuestId(qid, sizeof(qid), questPrefix, questCatId);

  bool complete = (flags & TXN_FLAG_QUEST_COMPLETE) != 0;
  if (complete) {
    int idx = findActiveQuest(qid);
    if (idx < 0) {
      result = TXN_RESULT_QUEST_NOT_FOUND;
      Serial.printf(">>> QUEST COMPLETE FAIL res=%u id=%s\n",
                    (unsigned)result, qid);
      return false;
    }
    int32_t reward = rubReward > 0 ? rubReward : 0;
    if (reward > 0) {
      playerMoney += reward;
      playerXP += reward;
    }
    for (uint8_t i = (uint8_t)idx; i + 1 < activeQuestCount; i++)
      strncpy(activeQuestIds[i], activeQuestIds[i + 1], 8);
    activeQuestCount--;
    paid = reward;
    result = TXN_RESULT_OK;
    Serial.printf(">>> QUEST COMPLETE OK id=%s +%ld RUB +%ld XP, balance=%ld\n",
                  qid, (long)reward, (long)reward, (long)playerMoney);
    return true;
  }

  if (findActiveQuest(qid) >= 0) {
    result = TXN_RESULT_QUEST_DUPLICATE;
    Serial.printf(">>> QUEST ACCEPT FAIL res=%u id=%s (already active)\n",
                  (unsigned)result, qid);
    return false;
  }
  if (activeQuestCount >= ACTIVE_QUESTS_MAX) {
    result = TXN_RESULT_UNKNOWN;
    Serial.println(">>> QUEST ACCEPT FAIL res=UNKNOWN (quest limit)");
    return false;
  }
  strncpy(activeQuestIds[activeQuestCount], qid, 8);
  activeQuestIds[activeQuestCount][8] = '\0';
  activeQuestCount++;
  paid = 0;
  result = TXN_RESULT_OK;
  Serial.printf(">>> QUEST ACCEPT OK id=%s cat=%u (active=%u)\n",
                qid, (unsigned)questCatId, (unsigned)activeQuestCount);
  return true;
}

void pollTxn() {
  if (!eepromPing(CH_TXN)) return;

  uint8_t block[TXN_BLOCK_SIZE];
  if (!eepromReadBlock(CH_TXN, TXN_EEPROM_BASE, block, TXN_BLOCK_SIZE)) return;
  if (!txn_validate_block(block)) return;
  if (txn_get_state(block) != TXN_STATE_PENDING) return;

  uint32_t txnId = txn_read_u32(block, TXN_OFF_TXN_ID);
  if (txnId == 0 || txnId == lastProcessedTxnId) return;

  int32_t amount = txn_read_i32(block, TXN_OFF_AMOUNT);
  uint16_t itemId = txn_read_u16(block, TXN_OFF_ITEM_ID);
  uint8_t opType = block[TXN_OFF_OP_TYPE];
  uint8_t flags = block[TXN_OFF_FLAGS];

  block[TXN_OFF_STATE] = TXN_STATE_PROCESSING;
  txn_recalc_crc(block);
  if (!eepromWriteBlock(CH_TXN, TXN_EEPROM_BASE, block, TXN_BLOCK_SIZE)) return;

  uint16_t result = TXN_RESULT_OK;
  int32_t paid = 0;
  bool handled = true;

  switch (opType) {
  case TXN_OP_PURCHASE:
    handlePurchase(amount, itemId, result, paid);
    break;
  case TXN_OP_BANK:
  case TXN_OP_ATM:
    handleBank(amount, flags, result, paid);
    break;
  case TXN_OP_QUEST: {
    char qprefix[9];
    memcpy(qprefix, block + TXN_OFF_RESERVED, 8);
    qprefix[8] = '\0';
    handleQuest(amount, itemId, flags, qprefix, result, paid);
    break;
  }
  default:
    handled = false;
    result = TXN_RESULT_NOT_IMPLEMENTED;
    Serial.printf(">>> op=%s not implemented in test sketch\n", txn_op_name(opType));
    break;
  }

  if (handled && result != TXN_RESULT_OK) {
    paid = 0;
  }

  if (finalizeTxn(txnId, block, result, paid)) {
    Serial.printf("TXN %s id=%lu res=%u written to EEPROM\n",
                  txn_op_name(opType), (unsigned long)txnId, (unsigned)result);
  }
}

void cmdHelp() {
  Serial.println(F(
    "PDA_TXN_Test — опрос CH0 для кассы\n"
    "  HELP  SCAN  DUMP  STATUS\n"
    "  SET_MONEY:1000  SET_LEVEL:2  RESET_TXN\n"
    "Auto poll CH0 every 200ms: PURCHASE, BANK, QUEST"));
}

void handleSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;

  if (line == "HELP") cmdHelp();
  else if (line == "SCAN") cmdScan();
  else if (line == "DUMP") cmdDump();
  else if (line == "STATUS") cmdStatus();
  else if (line == "RESET_TXN") {
    lastProcessedTxnId = 0;
    Serial.println("OK: last txn reset");
  }
  else if (line.startsWith("SET_MONEY:")) {
    playerMoney = line.substring(10).toInt();
    Serial.printf("OK: money=%ld\n", (long)playerMoney);
  }
  else if (line.startsWith("SET_LEVEL:")) {
    playerLevel = (uint8_t)line.substring(10).toInt();
    Serial.printf("OK: level=%u\n", (unsigned)playerLevel);
  }
  else Serial.println("Unknown cmd — HELP");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
  Serial.println(F("=== PDA TXN Test (minimal) ==="));
  Serial.printf("Board: %s | I2C SDA=%d SCL=%d\n", BOARD_LABEL, I2C_SDA, I2C_SCL);
  Serial.printf("CH0=universal/TXN  CH2=slot | poll=%ums\n", TXN_POLL_MS);
  cmdScan();
  cmdStatus();
  cmdHelp();
}

void loop() {
  handleSerial();
  if (millis() - lastPollMs >= TXN_POLL_MS) {
    lastPollMs = millis();
    pollTxn();
  }
}
