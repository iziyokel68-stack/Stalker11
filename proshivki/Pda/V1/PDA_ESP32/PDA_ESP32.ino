/**
 * ╔══════════════════════════════════════════════════╗
 * ║     S.T.A.L.K.E.R. ПДА — Прошивка ESP32 v2.7   ║
 * ║       Плата: ESP32-S3-N16R8 (DevKitC-1)         ║
 * ╠══════════════════════════════════════════════════╣
 * ║ CONFIG:FUNC / CONFIG:PRESET через USB Serial     ║
 * ║ Протокол: protocol.py v4.2                       ║
 * ║ Пины: MASTER_SPECIFICATION.md §3 (июнь 2026)     ║
 * ╠══════════════════════════════════════════════════╣
 * ║ Миграция v2.0→v2.1 (аппаратная валидация):       ║
 * ║ • M024 320×240 — tft_panel.h MADCTL 0x88 (v2.8)  ║
 * ║ • 4 кнопки DN/RT/OK/ESC (G4–G7); громкость — долгое RT/DN ║
 * ║ • G47/G48 не используются                                    ║
 * ║ • LED G15/G16, вибро G21, DFPlayer Serial2       ║
 * ║ • BU03 Serial1 G1/G2, BU03_PWR G42               ║
 * ║ • LoRa SPI G39/G40/G41, RST=G12 shared с TFT     ║
 * ║ • Ключи PWR/SND/VIB/BL — разрыв питания (не GPIO)║
 * ╠══════════════════════════════════════════════════╣
 * ║ UI (целевая модель batch 2, июнь 2026):          ║
 * ║ • 3 top-level страницы: 0 Главная, 1 Инвентарь,  ║
 * ║   2 Меню (подразделы: сопротивления / квесты /   ║
 * ║   достижения / сдаться — ВНИЗ + ОК, не отдельные ║
 * ║   страницы)                                      ║
 * ║ • Главная: HP/RAD/уровень/деньги + 2 строки notify║
 * ║ • LED красный G15: вспышка при уроне; мигание —  ║
 * ║   детектор (ур.25+) + аномалия ≤80 м (Фаза 5)    ║
 * ║ • LED зелёный G16: постоянно в ЗЗ                 ║
 * ║ • Вибро: whitelist событий (ключ VIB = питание);  ║
 * ║   смерть, RAD, level_up, достижение, ранг,      ║
 * ║   воскрешение, выброс/info; не деньги/покупки)   ║
 * ║ Текущий скетч: 5 страниц (вкл. UWB стр.3) —      ║
 * ║ полный перенход на 3+подменю — Фаза 5             ║
 * ╠══════════════════════════════════════════════════╣
 * ║ Советы по сборке (сессия 06.2026):                ║
 * ║ • DFPlayer + вибро — 5V BUS (TPS63020), не USB   ║
 * ║ • 5VIN — вход, не выход; USB ~500 mA мало          ║
 * ║ • DFPlayer: begin с реальным probe, без 5V — skip ║
 * ║ • LoRa: TFT_CS/LORA_CS idle HIGH, SPI transaction ║
 * ║ • LoRa RST=G12 общий с TFT — не пульсировать      ║
 * ║ • Старые пины WROOM / 5 кнопок — не использовать          ║
 * ╚══════════════════════════════════════════════════╝
 */

#include <string.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <DFRobotDFPlayerMini.h>
#include <Preferences.h>
#include <SPI.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_now.h>
// Заголовки в этой же папке (sketch folder)
#include "eeprom_protocol.h"
#include "mux_channels.h"

// =====================================================
// РАСПИНОВКА (ESP32-S3-N16R8) — MASTER_SPECIFICATION §3
// =====================================================
// TFT ILI9341 (SPI, shared bus with LoRa)
#define TFT_CS 10
#define TFT_DC 11
#define TFT_RST 12
#define TFT_SCK 13
#define TFT_MOSI 14
#define TFT_MISO 40
#include "tft_panel.h"

#define LORA_CS 39
#define LORA_RST TFT_RST // G12 — shared with TFT (no pulse at runtime)
#define LORA_DIO0 41

// Кнопки (4 шт, INPUT_PULLUP)
#define BTN_DN 4
#define BTN_RT 5
#define BTN_OK 6
#define BTN_ESC 7

// I2C (TCA9548A → EEPROM чипы)
#define PIN_SDA 9
#define PIN_SCL 8
#define TCA_ADDR 0x70

// DFPlayer Mini (Serial2)
#define DFPLAYER_TX 17 // ESP TX → DFPlayer RX
#define DFPLAYER_RX 18 // ESP RX ← DFPlayer TX
#define DFPLAYER_CMD_TIMEOUT_MS 400

// LED
#define LED_RED 15
#define LED_GREEN 16

// Вибромотор (NPN 2N2222, питание 5V BUS)
#define PIN_VIBRO 21

// BU03 UWB (Serial1)
#define BU03_RX 1
#define BU03_TX 2
#define BU03_PWR 42
#define HAS_BU03_PWR 1 // 0 = BU03 always powered

// =====================================================
// ЦВЕТА
// =====================================================
#define C_BLACK 0x0000
#define C_WHITE 0xFFFF
#define C_GREEN 0x07E0
#define C_YELLOW 0xFFE0
#if TFT_BGR_SWAP
#define C_RED 0x001F
#else
#define C_RED 0xF800
#endif
#define C_ORANGE 0xFD20
#define C_CYAN 0x07FF
#define C_PURPLE 0xA81F
#define C_DGRAY 0x3186
#define C_LGRAY 0xC618
#define C_BORDER 0x7BCF

// =====================================================
// ПРОТОКОЛ (protocol.py v4.2)
// =====================================================
#pragma pack(push, 1)
struct Packet {
  uint8_t emitter;
  uint8_t msg_type;
  int16_t val1;
  int16_t val2;
  int16_t val3;
};
#pragma pack(pop)

// Emitter (protocol.py)
#define EMITTER_PLAYER 1
#define EMITTER_ANOMALY 2
#define EMITTER_BASE 3

// Msg (protocol.py)
#define MSG_DAMAGE 1
#define MSG_RADIATION 3
#define MSG_ACK 6
#define MSG_SAFE_ZONE 7

// Доп. флаги Safe Zone
#define SZ_PROT_FLAG 0x7FFF
#define SZ_BEACON_FLAG 0x00FF

#define SZ_TIMEOUT_MS 5000 // Зона активна если пакет < 5 сек назад

// =====================================================
// СОСТОЯНИЕ ИГРОКА
// =====================================================
int playerHP = 1000;
int playerMaxHP = 1000;
int playerRad = 0;
int playerMaxRad = 1000;
int playerMoney = 1000;
int playerXP = 0;
int playerLevel = 1;
int playerDeaths = 0;
bool playerDead = false;
bool playerZombie = false;   // Роль зомби (RAD 100%, выброс, пси) — экран «ВЫ ЗОМБИ»; выход только «Сдаться»
bool admitPending = true;  // Блокировка всей системы ПДА (не в NVS); CmdSub.ADMIT / type=3 sub=6
// Регистрация: playerRegistered + playerName[] через ПК/EEPROM (programmer.py); до регистрации — пустой экран
// Античит: cheatShieldCount (NVS) — инкремент при экранировании ESP-NOW; без штрафа HP

// 8 типов сопротивлений: 0-6 = HP-типы, 7 = RAD
// ВЗРЫВ, КРОВЬ, ТЕРМО, ЭЛЕКТРО, ХИМИЯ, ПСИ, ГРАВИТ., RAD
int innateRes[8] = {0, 0, 0, 0, 0, 0, 0, 0};

const char *DMG_NAMES[] = {"ВЗРЫВ", "КРОВЬ", "ТЕРМО",  "ЭЛЕКТРО",
                           "ХИМИЯ", "ПСИ",   "ГРАВИТ."};

// =====================================================
// КОНФИГ ПДА (хранится в NVS, приходит через CONFIG:)
// =====================================================
// Флаги функций (битмаска, 0xFF = все включены)
// Бит 0=HP, 1=RAD, 2=Деньги, 3=Броня, 4=Арты, 5=Аномалии, 6=Уровни,
// 7=Расходники
uint8_t cfgFuncFlags = 0xFF;

// Пресет начальных данных (шкала HP/RAD = 1000; на TFT — полосы %, без цифр HP/RAD)
int cfgMaxHP = 1000;
int cfgStartHP = 1000;
int cfgMaxRad = 1000;
int cfgStartMoney = 1000;
int cfgStartLevel = 2; // после регистрации (инструктаж)
int cfgStartXP = 0;
int cfgBaseProt[8] = {0, 0, 0, 0, 0, 0, 0, 0}; // Базовые защиты %
uint8_t dfVolume = 20;                          // DFPlayer 0–30, радио + уведомления

// =====================================================
// ПРОГРЕССИЯ v4 — MASTER_SPEC §8.5, hardware/PROGRESSION.txt
// =====================================================
void setEvent(const char *text, uint16_t color);
void checkLevelUps();
void checkRankReadyNotify();
void saveState();
void completeRegistration(const char *name);
bool applyDfVolume();
extern Preferences prefs;
extern bool needFullRedraw;
extern bool isAudioReady;
extern DFRobotDFPlayerMini myDFPlayer;

#define MAX_LEVEL 100
#define REGISTRATION_LEVEL 2
#define LVL_STORE 2
#define LVL_ATM 3
#define LVL_ARTIFACT1 2
#define LVL_HIDDEN_QUEST 20
#define LVL_DETECTOR 25 // ДЕТЕКТОР: ур.25+ и пакет аномалии рядом (ESP-NOW) → мигание LED
#define LVL_GLOBAL_MSG 50
#define DETECTOR_SIGNAL_TIMEOUT_MS 2500 // индикатор гаснет без пакетов аномалии

struct RankTier {
  uint8_t minLevel;
  const char *title;
  uint16_t rubReward;
  uint8_t discountPct;
};

const RankTier RANK_TIERS[] = {
    {1, "НОВИЧОК", 0, 0},       {10, "БРОДЯГА", 800, 3},
    {25, "СТАЛКЕР", 1500, 6},   {40, "ВЕТЕРАН", 2500, 10},
    {60, "МАСТЕР", 4000, 14},   {80, "ЛЕГЕНДА", 6000, 18},
};
#define NUM_RANKS 6

enum AchievementId : uint8_t {
  ACH_REG = 0,
  ACH_FIRST_ANOMALY,
  ACH_FIRST_ARTIFACT,
  ACH_FIRST_QUEST,
  ACH_SURVIVE_2H,
  ACH_COUNT
};

struct AchDef {
  const char *name;
  int xp;
  int rub;
};

const AchDef ACH_TABLE[ACH_COUNT] = {
    {"РЕГИСТРАЦИЯ", 80, 150},
    {"ПЕРВАЯ АНОМАЛИЯ", 150, 200},
    {"ПЕРВЫЙ АРТЕФАКТ", 120, 180},
    {"ПЕРВЫЙ КВЕСТ", 200, 300},
    {"2Ч БЕЗ СМЕРТИ", 25, 50},
};

int playerRank = 0;
uint32_t achievementFlags = 0;
bool playerRegistered = false;
int playerEventId = 0;                 // ID игрока на событии (LoRa to=)
char playerAssignedUid[16] = "";       // UID, выданный мастером, не MAC
char playerName[25] = "";
char playerCallsign[16] = "";
char playerGroup[16] = "";
uint32_t sessionStartMs = 0;
uint32_t lastDeathAtLevelUp = 0;
bool surviveAchPending = false;
uint8_t discoveredMacs[8][6];
uint8_t discoveredMacCount = 0;
bool rankReadyNotified[NUM_RANKS] = {false};
uint32_t achVibroUntilMs = 0;

#define ACTIVE_TASKS_MAX 4
struct ActiveTaskStub {
  char id[12];
  char title[24];
  char shortDesc[40];
};
uint8_t activeTaskCount = 0;
ActiveTaskStub activeTasks[ACTIVE_TASKS_MAX];

int xpPerLevel(int lvl) {
  if (lvl < 10)
    return 90 + lvl * 32;
  if (lvl < 40)
    return 200 + (lvl - 9) * 8;
  if (lvl < 70)
    return 420 + (lvl - 39) * 4;
  return 460 + (lvl - 69) * 3;
}

int xpTotalForLevel(int targetLevel) {
  int total = 0;
  for (int l = 1; l < targetLevel; l++)
    total += xpPerLevel(l);
  return total;
}

// Награда RUB за уровень (hardware/PROGRESSION.txt §2)
int rubRewardOnLevelUp(int lvl) {
  if (lvl >= 1 && lvl <= 4)
    return 500;
  if (lvl == 5)
    return 2000;
  if (lvl >= 6 && lvl <= 9)
    return 500;
  if (lvl >= 10 && lvl <= 12)
    return 1000;
  if (lvl % 5 == 0)
    return 1000;
  return 500;
}

const char *getRankTitle() {
  if (playerRank >= 0 && playerRank < NUM_RANKS)
    return RANK_TIERS[playerRank].title;
  return "???";
}

uint8_t getRankDiscountPct() {
  if (playerRank >= 0 && playerRank < NUM_RANKS)
    return RANK_TIERS[playerRank].discountPct;
  return 0;
}

const char *getLevelTitle() {
  if (playerLevel >= 100)
    return "ХРОНИКЁР ЗОНЫ";
  return getRankTitle();
}

bool hasAchievement(AchievementId id) {
  return (achievementFlags & (1UL << id)) != 0;
}

void grantAchievement(AchievementId id) {
  if (id >= ACH_COUNT || hasAchievement(id))
    return;
  if (!(cfgFuncFlags & (1 << 6)))
    return;
  achievementFlags |= (1UL << id);
  const AchDef &a = ACH_TABLE[id];
  playerXP += a.xp;
  playerMoney += a.rub;
  char buf[56];
  snprintf(buf, sizeof(buf), "ДОСТИЖЕНИЕ: %s +%dXP +%dRUB", a.name, a.xp,
           a.rub);
  setEvent(buf, C_PURPLE);
  achVibroUntilMs = millis() + 350;
  saveState();
  checkLevelUps();
  checkRankReadyNotify();
}

void checkRankReadyNotify() {
  if (!(cfgFuncFlags & (1 << 6)))
    return;
  int next = playerRank + 1;
  if (next >= NUM_RANKS || rankReadyNotified[next])
    return;
  if (playerLevel < RANK_TIERS[next].minLevel)
    return;
  rankReadyNotified[next] = true;
  char buf[40];
  snprintf(buf, sizeof(buf), "РАНГ ДОСТУПЕН: %s", RANK_TIERS[next].title);
  setEvent(buf, C_YELLOW);
  achVibroUntilMs = millis() + 500;
}

void checkLevelUps() {
  if (!(cfgFuncFlags & (1 << 6)))
    return;
  while (playerLevel < MAX_LEVEL &&
         playerXP >= xpTotalForLevel(playerLevel + 1)) {
    playerLevel++;
    int reward = rubRewardOnLevelUp(playerLevel);
    playerMoney += reward;
    char buf[40];
    snprintf(buf, sizeof(buf), "LV%u +%d RUB", playerLevel, reward);
    setEvent(buf, C_GREEN);
  }
  saveState();
  checkRankReadyNotify();
}

void completeRegistration(const char *name) {
  bool nameUpdated = false;
  if (name && name[0]) {
    strncpy(playerName, name, sizeof(playerName) - 1);
    playerName[sizeof(playerName) - 1] = '\0';
    nameUpdated = true;
  }
  if (playerRegistered) {
    if (nameUpdated)
      saveState();
    return;
  }
  playerRegistered = true;
  if (playerLevel < REGISTRATION_LEVEL)
    playerLevel = REGISTRATION_LEVEL;
  grantAchievement(ACH_REG);
  saveState();
}

void confirmNextRank() {
  if (playerRank + 1 >= NUM_RANKS)
    return;
  if (playerLevel < RANK_TIERS[playerRank + 1].minLevel)
    return;
  playerRank++;
  const RankTier &tier = RANK_TIERS[playerRank];
  playerMoney += tier.rubReward;
  char buf[48];
  snprintf(buf, sizeof(buf), "РАНГ: %s +%uRUB %u%%", tier.title,
           tier.rubReward, tier.discountPct);
  setEvent(buf, C_CYAN);
  saveState();
  checkRankReadyNotify();
}

bool isMacDiscovered(const uint8_t *mac) {
  for (uint8_t i = 0; i < discoveredMacCount; i++) {
    if (memcmp(discoveredMacs[i], mac, 6) == 0)
      return true;
  }
  return false;
}

void noteAnomalyDiscovery(const uint8_t *mac) {
  if (isMacDiscovered(mac))
    return;
  if (discoveredMacCount < 8)
    memcpy(discoveredMacs[discoveredMacCount++], mac, 6);
  if (!hasAchievement(ACH_FIRST_ANOMALY))
    grantAchievement(ACH_FIRST_ANOMALY);
  else {
    playerXP += 40;
    checkLevelUps();
  }
  saveState();
}

bool canUseStore() { return playerLevel >= LVL_STORE; }
bool canUseAtm() { return playerLevel >= LVL_ATM; }
bool canUseArtifact1() { return playerLevel >= LVL_ARTIFACT1; }
bool canUseHiddenQuest() { return playerLevel >= LVL_HIDDEN_QUEST; }
bool canUseDetector() { return playerLevel >= LVL_DETECTOR; }

int8_t lastAnomalyRssi = -100;
uint32_t lastAnomalySignalMs = 0;

void noteAnomalySignal(const esp_now_recv_info_t *info) {
  if (info && info->rx_ctrl)
    lastAnomalyRssi = info->rx_ctrl->rssi;
  lastAnomalySignalMs = millis();
}

bool isAnomalyDetectorActive() {
  if (!canUseDetector() || lastAnomalySignalMs == 0)
    return false;
  return (millis() - lastAnomalySignalMs) < DETECTOR_SIGNAL_TIMEOUT_MS;
}
bool canUseGlobalMsg() { return playerLevel >= LVL_GLOBAL_MSG; }

// =====================================================
// EEPROM — терминалы↔ПДА транзакции (CH1 universal, блок @0x80)
// =====================================================
#define EEPROM_DEV EEPROM_ADDR_DEFAULT
#define UNIVERSAL_SLOT MUX_CH_UNIVERSAL
#define EEPROM_WR_DLY 5

// UI inventory row 0..3 → TCA channel (броня, арт 1..3)
static const uint8_t INV_SLOT_MUX_CH[4] = {
  MUX_CH_ARMOR, MUX_CH_ARTIFACT_1, MUX_CH_ARTIFACT_2, MUX_CH_ARTIFACT_3
};

uint32_t lastProcessedTxnId = 0;
uint32_t lastTxnPollMs = 0;
#define TXN_POLL_MS 200

bool tcaSelect(uint8_t channel) {
  if (channel > 7)
    return false;
  Wire.beginTransmission(TCA_ADDR);
  Wire.write((uint8_t)(1 << channel));
  return Wire.endTransmission() == 0;
}

bool eepromWriteByteChUniversal(uint16_t addr, uint8_t val) {
  if (!tcaSelect(UNIVERSAL_SLOT))
    return false;
  Wire.beginTransmission(EEPROM_DEV);
  Wire.write((uint8_t)(addr >> 8));
  Wire.write((uint8_t)(addr & 0xFF));
  Wire.write(val);
  uint8_t err = Wire.endTransmission();
  delay(EEPROM_WR_DLY);
  return err == 0;
}

bool eepromReadByteChUniversal(uint16_t addr, uint8_t &val) {
  if (!tcaSelect(UNIVERSAL_SLOT))
    return false;
  Wire.beginTransmission(EEPROM_DEV);
  Wire.write((uint8_t)(addr >> 8));
  Wire.write((uint8_t)(addr & 0xFF));
  if (Wire.endTransmission() != 0)
    return false;
  Wire.requestFrom((uint8_t)EEPROM_DEV, (uint8_t)1);
  if (!Wire.available())
    return false;
  val = Wire.read();
  return true;
}

bool eepromReadBlockChUniversal(uint16_t addr, uint8_t *buf, uint8_t len) {
  for (uint8_t i = 0; i < len; i++) {
    if (!eepromReadByteChUniversal(addr + i, buf[i]))
      return false;
  }
  return true;
}

bool eepromWriteBlockChUniversal(uint16_t addr, const uint8_t *data, uint8_t len) {
  for (uint8_t i = 0; i < len; i++) {
    if (!eepromWriteByteChUniversal(addr + i, data[i]))
      return false;
  }
  return true;
}

bool chipPresentOnChUniversal() {
  if (!tcaSelect(UNIVERSAL_SLOT))
    return false;
  Wire.beginTransmission(EEPROM_DEV);
  return Wire.endTransmission() == 0;
}

int calcShopPrice(int basePrice) {
  int disc = getRankDiscountPct();
  return basePrice * (100 - disc) / 100;
}

bool txnWriteStateChUniversal(uint8_t state) {
  uint8_t block[TXN_BLOCK_SIZE];
  if (!eepromReadBlockChUniversal(TXN_EEPROM_BASE, block, TXN_BLOCK_SIZE))
    return false;
  block[TXN_OFF_STATE] = state;
  txn_recalc_crc(block);
  return eepromWriteBlockChUniversal(TXN_EEPROM_BASE, block, TXN_BLOCK_SIZE);
}

void txnFailLocked(TxnOutcome &o) {
  o.result = TXN_RESULT_SYSTEM_LOCKED;
  o.paid = 0;
  o.balanceAfter = playerMoney;
  snprintf(o.eventMsg, sizeof(o.eventMsg), "ТЕРМИНАЛ: БЛОКИРОВКА");
  o.eventColor = C_RED;
}

void txnFailLevel(TxnOutcome &o, const char *svc) {
  o.result = TXN_RESULT_LEVEL_TOO_LOW;
  o.paid = 0;
  o.balanceAfter = playerMoney;
  snprintf(o.eventMsg, sizeof(o.eventMsg), "%s: МАЛО УРОВНЯ", svc);
  o.eventColor = C_ORANGE;
}

void txnPrintReport(uint8_t opType, uint32_t txnId, const TxnOutcome &o) {
  Serial.printf("TXN_REPORT:op=%s,txn_id=%lu,result=%s,result_code=%u,"
                "paid=%ld,balance=%ld,msg=%s\n",
                txn_op_name(opType), (unsigned long)txnId,
                txn_result_name(o.result), (unsigned)o.result, (long)o.paid,
                (long)o.balanceAfter, o.eventMsg[0] ? o.eventMsg : "-");
}

bool txnFinalizeChUniversal(uint32_t txnId, uint8_t *block, const TxnOutcome &o) {
  if (!eepromReadBlockChUniversal(TXN_EEPROM_BASE, block, TXN_BLOCK_SIZE))
    return false;
  txn_write_i32(block, TXN_OFF_BALANCE, o.balanceAfter);
  txn_write_u16(block, TXN_OFF_RESULT, o.result);
  txn_write_i32(block, TXN_OFF_PAID, o.paid);
  block[TXN_OFF_FLAGS] = o.flagsOut;
  block[TXN_OFF_STATE] =
      (o.result == TXN_RESULT_OK) ? TXN_STATE_SUCCESS : TXN_STATE_FAILED;
  txn_recalc_crc(block);
  if (!eepromWriteBlockChUniversal(TXN_EEPROM_BASE, block, TXN_BLOCK_SIZE))
    return false;
  lastProcessedTxnId = txnId;
  prefs.begin("pda", false);
  prefs.putUInt("last_txn", lastProcessedTxnId);
  prefs.end();
  if (o.eventMsg[0]) {
    setEvent(o.eventMsg, o.eventColor);
    needFullRedraw = true;
  }
  return true;
}

void txnHandlePurchase(int32_t baseAmount, uint16_t itemId, TxnOutcome &o) {
  o.flagsOut = 0;
  if (admitPending || playerDead) {
    txnFailLocked(o);
    return;
  }
  if (!canUseStore()) {
    txnFailLevel(o, "МАГАЗИН");
    return;
  }
  if (baseAmount <= 0) {
    o.result = TXN_RESULT_UNKNOWN;
    o.paid = 0;
    o.balanceAfter = playerMoney;
    snprintf(o.eventMsg, sizeof(o.eventMsg), "ПОКУПКА: ОШИБКА");
    o.eventColor = C_RED;
    return;
  }
  o.paid = calcShopPrice((int)baseAmount);
  if (playerMoney < o.paid) {
    o.result = TXN_RESULT_INSUFFICIENT_FUNDS;
    o.paid = 0;
    o.balanceAfter = playerMoney;
    snprintf(o.eventMsg, sizeof(o.eventMsg), "НЕ ХВАТАЕТ: %d RUB",
             (int)baseAmount);
    o.eventColor = C_RED;
    return;
  }
  playerMoney -= o.paid;
  o.balanceAfter = playerMoney;
  o.result = TXN_RESULT_OK;
  if (getRankDiscountPct() > 0)
    o.flagsOut = TXN_FLAG_DISCOUNT;
  saveState();
  snprintf(o.eventMsg, sizeof(o.eventMsg), "ПОКУПКА #%u: -%d RUB",
           (unsigned)itemId, (int)o.paid);
  o.eventColor = C_GREEN;
}

void txnHandleBank(int32_t amount, uint8_t flags, TxnOutcome &o) {
  o.flagsOut = flags;
  if (admitPending || playerDead) {
    txnFailLocked(o);
    return;
  }
  if (!canUseAtm()) {
    txnFailLevel(o, "БАНК");
    return;
  }
  if (amount <= 0) {
    o.result = TXN_RESULT_UNKNOWN;
    o.paid = 0;
    o.balanceAfter = playerMoney;
    return;
  }
  bool deposit = (flags & TXN_FLAG_BANK_DEPOSIT) != 0;
  if (deposit) {
    if (playerMoney < amount) {
      o.result = TXN_RESULT_INSUFFICIENT_FUNDS;
      o.paid = 0;
      o.balanceAfter = playerMoney;
      snprintf(o.eventMsg, sizeof(o.eventMsg), "БАНК: НЕ ХВАТАЕТ");
      o.eventColor = C_RED;
      return;
    }
    playerMoney -= amount;
    o.paid = amount;
    o.balanceAfter = playerMoney;
    o.result = TXN_RESULT_OK;
    saveState();
    snprintf(o.eventMsg, sizeof(o.eventMsg), "ВКЛАД: -%d RUB", (int)amount);
    o.eventColor = C_CYAN;
  } else {
    playerMoney += amount;
    o.paid = amount;
    o.balanceAfter = playerMoney;
    o.result = TXN_RESULT_OK;
    saveState();
    snprintf(o.eventMsg, sizeof(o.eventMsg), "СНЯТИЕ: +%d RUB", (int)amount);
    o.eventColor = C_CYAN;
  }
}

int findActiveQuest(const char *qid) {
  for (uint8_t i = 0; i < activeTaskCount; i++) {
    if (strncmp(activeTasks[i].id, qid, sizeof(activeTasks[i].id)) == 0)
      return (int)i;
  }
  return -1;
}

void txnHandleQuest(int32_t rubReward, uint16_t questCatId, uint8_t flags,
                    const char *questIdPrefix, TxnOutcome &o) {
  o.flagsOut = flags;
  char qid[12];
  memset(qid, 0, sizeof(qid));
  if (questIdPrefix[0]) {
    strncpy(qid, questIdPrefix, 8);
    qid[8] = '\0';
  } else {
    snprintf(qid, sizeof(qid), "q%u", (unsigned)questCatId);
  }

  if (admitPending || playerDead) {
    txnFailLocked(o);
    return;
  }

  bool complete = (flags & TXN_FLAG_QUEST_COMPLETE) != 0;
  if (complete) {
    int idx = findActiveQuest(qid);
    if (idx < 0) {
      o.result = TXN_RESULT_QUEST_NOT_FOUND;
      o.paid = 0;
      o.balanceAfter = playerMoney;
      snprintf(o.eventMsg, sizeof(o.eventMsg), "КВЕСТ: НЕТ ЗАДАНИЯ");
      o.eventColor = C_ORANGE;
      return;
    }
    int reward = rubReward > 0 ? (int)rubReward : 0;
    if (reward > 0) {
      playerMoney += reward;
      playerXP += reward;
      checkLevelUps();
    }
    for (uint8_t i = (uint8_t)idx; i + 1 < activeTaskCount; i++)
      activeTasks[i] = activeTasks[i + 1];
    if (activeTaskCount > 0)
      activeTaskCount--;
    if (!hasAchievement(ACH_FIRST_QUEST))
      grantAchievement(ACH_FIRST_QUEST);
    o.paid = reward;
    o.balanceAfter = playerMoney;
    o.result = TXN_RESULT_OK;
    saveState();
    snprintf(o.eventMsg, sizeof(o.eventMsg), "КВЕСТ СДАН: +%d RUB", reward);
    o.eventColor = C_GREEN;
    return;
  }

  if (findActiveQuest(qid) >= 0) {
    o.result = TXN_RESULT_QUEST_DUPLICATE;
    o.paid = 0;
    o.balanceAfter = playerMoney;
    snprintf(o.eventMsg, sizeof(o.eventMsg), "КВЕСТ: УЖЕ АКТИВЕН");
    o.eventColor = C_ORANGE;
    return;
  }
  if (activeTaskCount >= ACTIVE_TASKS_MAX) {
    o.result = TXN_RESULT_UNKNOWN;
    o.paid = 0;
    o.balanceAfter = playerMoney;
    snprintf(o.eventMsg, sizeof(o.eventMsg), "КВЕСТ: ЛИМИТ");
    o.eventColor = C_RED;
    return;
  }
  ActiveTaskStub &t = activeTasks[activeTaskCount++];
  strncpy(t.id, qid, sizeof(t.id) - 1);
  snprintf(t.title, sizeof(t.title), "Задание #%u", (unsigned)questCatId);
  strncpy(t.shortDesc, "Терминал квестов", sizeof(t.shortDesc) - 1);
  o.paid = 0;
  o.balanceAfter = playerMoney;
  o.result = TXN_RESULT_OK;
  saveState();
  snprintf(o.eventMsg, sizeof(o.eventMsg), "КВЕСТ: %s", t.title);
  o.eventColor = C_YELLOW;
}

void txnHandleAdmit(TxnOutcome &o) {
  o.flagsOut = 0;
  o.paid = 0;
  o.balanceAfter = playerMoney;
  if (!admitPending) {
    o.result = TXN_RESULT_OK;
    snprintf(o.eventMsg, sizeof(o.eventMsg), "ДОПУСК: УЖЕ ЕСТЬ");
    o.eventColor = C_LGRAY;
    return;
  }
  admitPending = false;
  completeRegistration(nullptr);
  o.result = TXN_RESULT_OK;
  snprintf(o.eventMsg, sizeof(o.eventMsg), "ДОПУСК В ИГРУ");
  o.eventColor = C_GREEN;
  needFullRedraw = true;
}

void txnHandleRegister(uint16_t playerId, const char *uidPrefix, TxnOutcome &o) {
  o.flagsOut = 0;
  o.paid = 0;
  o.balanceAfter = playerMoney;
  char name[25] = {0};
  uint8_t ext[24];
  memset(ext, 0, sizeof(ext));
  if (eepromReadBlockChUniversal(EEPROM_CHIP_EXT_BASE, ext, sizeof(ext))) {
    memcpy(name, ext, sizeof(ext));
    name[24] = '\0';
  }
  playerEventId = (int)playerId;
  if (uidPrefix && uidPrefix[0]) {
    if (strchr(uidPrefix, '-')) {
      strncpy(playerAssignedUid, uidPrefix, sizeof(playerAssignedUid) - 1);
    } else {
      snprintf(playerAssignedUid, sizeof(playerAssignedUid), "%s-%04u",
               uidPrefix, (unsigned)playerId);
    }
    playerAssignedUid[sizeof(playerAssignedUid) - 1] = '\0';
  } else {
    snprintf(playerAssignedUid, sizeof(playerAssignedUid), "%04u",
             (unsigned)playerId);
  }
  completeRegistration(name[0] ? name : nullptr);
  saveState();
  o.result = TXN_RESULT_OK;
  snprintf(o.eventMsg, sizeof(o.eventMsg), "РЕГ №%u", (unsigned)playerId);
  o.eventColor = C_GREEN;
  needFullRedraw = true;
}

void pollEepromTransaction() {
  if (!chipPresentOnChUniversal())
    return;

  uint8_t block[TXN_BLOCK_SIZE];
  if (!eepromReadBlockChUniversal(TXN_EEPROM_BASE, block, TXN_BLOCK_SIZE))
    return;
  if (!txn_validate_block(block))
    return;

  uint8_t state = txn_get_state(block);
  if (state != TXN_STATE_PENDING)
    return;

  uint32_t txnId = txn_read_u32(block, TXN_OFF_TXN_ID);
  if (txnId == 0 || txnId == lastProcessedTxnId)
    return;

  int32_t amount = txn_read_i32(block, TXN_OFF_AMOUNT);
  uint16_t itemId = txn_read_u16(block, TXN_OFF_ITEM_ID);
  uint8_t opType = block[TXN_OFF_OP_TYPE];
  uint8_t flags = block[TXN_OFF_FLAGS];

  if (!txnWriteStateChUniversal(TXN_STATE_PROCESSING))
    return;

  TxnOutcome outcome = {};
  outcome.balanceAfter = playerMoney;

  switch (opType) {
  case TXN_OP_PURCHASE:
    txnHandlePurchase(amount, itemId, outcome);
    break;
  case TXN_OP_BANK:
  case TXN_OP_ATM:
    txnHandleBank(amount, flags, outcome);
    break;
  case TXN_OP_QUEST: {
    char qprefix[9];
    memcpy(qprefix, block + TXN_OFF_RESERVED, 8);
    qprefix[8] = '\0';
    txnHandleQuest(amount, itemId, flags, qprefix, outcome);
    break;
  }
  case TXN_OP_ADMIT:
    txnHandleAdmit(outcome);
    break;
  case TXN_OP_REGISTER: {
    char uidpref[9];
    memcpy(uidpref, block + TXN_OFF_RESERVED, 8);
    uidpref[8] = '\0';
    txnHandleRegister(itemId, uidpref, outcome);
    break;
  }
  default:
    outcome.result = TXN_RESULT_NOT_IMPLEMENTED;
    outcome.paid = 0;
    snprintf(outcome.eventMsg, sizeof(outcome.eventMsg), "TXN: НЕ ПОДДЕРЖ.");
    outcome.eventColor = C_RED;
    break;
  }

  if (txnFinalizeChUniversal(txnId, block, outcome))
    txnPrintReport(opType, txnId, outcome);
}

// =====================================================
// NVS — ЗАГРУЗКА / СОХРАНЕНИЕ
// =====================================================
Preferences prefs;

void loadState() {
  prefs.begin("pda", true);
  playerHP = prefs.getInt("hp", cfgStartHP);
  playerMaxHP = prefs.getInt("max_hp", cfgMaxHP);
  playerRad = prefs.getInt("rad", 0);
  playerMaxRad = prefs.getInt("max_rad", cfgMaxRad);
  playerMoney = prefs.getInt("money", cfgStartMoney);
  playerXP = prefs.getInt("xp", cfgStartXP);
  playerLevel = prefs.getInt("lvl", cfgStartLevel);
  playerDeaths = prefs.getInt("deaths", 0);
  playerDead = prefs.getBool("dead", false);
  playerRank = prefs.getInt("rank", 0);
  achievementFlags = prefs.getUInt("ach", 0);
  playerRegistered = prefs.getBool("reg", false);
  playerEventId = prefs.getInt("eid", 0);
  prefs.getString("auid", playerAssignedUid, sizeof(playerAssignedUid));
  prefs.getString("pname", playerName, sizeof(playerName));
  prefs.getString("pcsign", playerCallsign, sizeof(playerCallsign));
  prefs.getString("pgroup", playerGroup, sizeof(playerGroup));
  discoveredMacCount = prefs.getUChar("mac_n", 0);
  if (discoveredMacCount > 8)
    discoveredMacCount = 8;
  size_t got = prefs.getBytes("mac_b", discoveredMacs, sizeof(discoveredMacs));
  if (got != sizeof(discoveredMacs))
    discoveredMacCount = 0;
  // Сопротивления
  for (int i = 0; i < 8; i++) {
    char key[8];
    snprintf(key, sizeof(key), "res%d", i);
    innateRes[i] = prefs.getInt(key, cfgBaseProt[i]);
  }
  lastProcessedTxnId = prefs.getUInt("last_txn", 0);
  prefs.end();
}

void saveState() {
  prefs.begin("pda", false);
  prefs.putInt("hp", playerHP);
  prefs.putInt("max_hp", playerMaxHP);
  prefs.putInt("rad", playerRad);
  prefs.putInt("max_rad", playerMaxRad);
  prefs.putInt("money", playerMoney);
  prefs.putInt("xp", playerXP);
  prefs.putInt("lvl", playerLevel);
  prefs.putInt("deaths", playerDeaths);
  prefs.putBool("dead", playerDead);
  prefs.putInt("rank", playerRank);
  prefs.putUInt("ach", achievementFlags);
  prefs.putBool("reg", playerRegistered);
  prefs.putInt("eid", playerEventId);
  prefs.putString("auid", playerAssignedUid);
  prefs.putString("pname", playerName);
  prefs.putString("pcsign", playerCallsign);
  prefs.putString("pgroup", playerGroup);
  prefs.putUChar("mac_n", discoveredMacCount);
  prefs.putBytes("mac_b", discoveredMacs, discoveredMacCount * 6);
  for (int i = 0; i < 8; i++) {
    char key[8];
    snprintf(key, sizeof(key), "res%d", i);
    prefs.putInt(key, innateRes[i]);
  }
  prefs.end();
}

void loadConfig() {
  prefs.begin("pda_cfg", true);
  cfgFuncFlags = prefs.getUChar("flags", 0xFF);
  cfgMaxHP = prefs.getInt("maxhp", 1000);
  cfgStartHP = prefs.getInt("starthp", 1000);
  cfgMaxRad = prefs.getInt("maxrad", 1000);
  cfgStartMoney = prefs.getInt("money", 1000);
  cfgStartLevel = prefs.getInt("lvl", 1);
  cfgStartXP = prefs.getInt("xp", 0);
  for (int i = 0; i < 8; i++) {
    char key[8];
    snprintf(key, sizeof(key), "bp%d", i);
    cfgBaseProt[i] = prefs.getInt(key, 0);
  }
  dfVolume = prefs.getUChar("vol", 20);
  if (dfVolume > 30)
    dfVolume = 30;
  prefs.end();
}

void saveConfig() {
  prefs.begin("pda_cfg", false);
  prefs.putUChar("flags", cfgFuncFlags);
  prefs.putInt("maxhp", cfgMaxHP);
  prefs.putInt("starthp", cfgStartHP);
  prefs.putInt("maxrad", cfgMaxRad);
  prefs.putInt("money", cfgStartMoney);
  prefs.putInt("lvl", cfgStartLevel);
  prefs.putInt("xp", cfgStartXP);
  for (int i = 0; i < 8; i++) {
    char key[8];
    snprintf(key, sizeof(key), "bp%d", i);
    prefs.putInt(key, cfgBaseProt[i]);
  }
  prefs.putUChar("vol", dfVolume);
  prefs.end();
}

// =====================================================
// SERIAL CONFIG PARSER (от programmer.py)
// =====================================================
// Формат: CONFIG:FUNC:flags=255
//         CONFIG:PRESET:maxhp=100,starthp=100,...,r0=10,...,r7=5

int parseIntValue(const String &s, const String &key) {
  int idx = s.indexOf(key + "=");
  if (idx < 0)
    return -1;
  int start = idx + key.length() + 1;
  int end = s.indexOf(',', start);
  if (end < 0)
    end = s.length();
  return s.substring(start, end).toInt();
}

String parseStringValue(const String &s, const String &key) {
  int idx = s.indexOf(key + "=");
  if (idx < 0)
    return "";
  int start = idx + key.length() + 1;
  int end = s.indexOf(',', start);
  if (end < 0)
    end = s.length();
  String v = s.substring(start, end);
  v.trim();
  return v;
}

void handleSerialConfig(const String &line) {
  if (line.startsWith("STALKER_WHO")) {
    Serial.println("STALKER:PDA:v2.7");
    return;
  }

  if (line.startsWith("CONFIG:UID")) {
    uint64_t mac = ESP.getEfuseMac();
    char buf[20];
    snprintf(buf, sizeof(buf), "%012llX", (unsigned long long)mac);
    Serial.print("UID:");
    Serial.println(buf);
    return;
  }

  if (line.startsWith("CONFIG:REGISTER")) {
    String data = "";
    if (line.startsWith("CONFIG:REGISTER:"))
      data = line.substring(strlen("CONFIG:REGISTER:"));
    String nm = parseStringValue(data, "name");
    String cs = parseStringValue(data, "callsign");
    String gr = parseStringValue(data, "group");
    if (cs.length()) {
      strncpy(playerCallsign, cs.c_str(), sizeof(playerCallsign) - 1);
      playerCallsign[sizeof(playerCallsign) - 1] = '\0';
    }
    if (gr.length()) {
      strncpy(playerGroup, gr.c_str(), sizeof(playerGroup) - 1);
      playerGroup[sizeof(playerGroup) - 1] = '\0';
    }
    completeRegistration(nm.length() ? nm.c_str() : nullptr);
    saveState();
    Serial.println("OK");
    return;
  }

  if (line.startsWith("CONFIG:ADMIT")) {
    admitPending = false;
    completeRegistration(nullptr);
    needFullRedraw = true;
    setEvent("ДОПУСК В ИГРУ", C_GREEN);
    Serial.println("OK");
    return;
  }

  if (line.startsWith("CONFIG:REVIVE")) {
    if (playerZombie) {
      Serial.println("ERROR:ZOMBIE");
      return;
    }
    if (!playerDead) {
      Serial.println("ERROR:ALIVE");
      return;
    }
    playerDead = false;
    playerHP = playerMaxHP;
    playerRad = 0;
    saveState();
    needFullRedraw = true;
    setEvent("СВЯЗЬ ВОССТАНОВЛЕНА", C_GREEN);
    Serial.println("OK");
    return;
  }

  if (line.startsWith("CONFIG:BROADCAST:")) {
    String text = line.substring(strlen("CONFIG:BROADCAST:"));
    text.trim();
    if (text.length() > 47)
      text = text.substring(0, 47);
    if (text.length())
      setEvent(text.c_str(), C_YELLOW);
    Serial.println("OK");
    return;
  }

  if (line.startsWith("CONFIG:EMISSION:")) {
    String data = line.substring(strlen("CONFIG:EMISSION:"));
    int timer_min = parseIntValue(data, "timer_min");
    int dur_min = parseIntValue(data, "duration_min");
    int timer = parseIntValue(data, "timer");
    int dur = parseIntValue(data, "duration");
    if (timer_min >= 0)
      timer = timer_min * 60;
    if (dur_min >= 0)
      dur = dur_min * 60;
    if (timer < 0)
      timer = 0;
    if (dur < 0)
      dur = 0;
    char buf[48];
    snprintf(buf, sizeof(buf), "ВЫБРОС %dм / %dм", timer / 60, dur / 60);
    setEvent(buf, C_RED);
    Serial.println("OK");
    return;
  }

  if (line.startsWith("CONFIG:VOLUME:")) {
    String data = line.substring(strlen("CONFIG:VOLUME:"));
    int level = parseIntValue(data, "level");
    if (level < 0)
      level = parseIntValue(data, "vol");
    if (level >= 0) {
      dfVolume = (uint8_t)constrain(level, 0, 30);
      saveConfig();
      applyDfVolume();
    }
    Serial.println("OK");
    return;
  }

  if (line.startsWith("CONFIG:RADIO:")) {
    String data = line.substring(strlen("CONFIG:RADIO:"));
    int track = parseIntValue(data, "track");
    int vol = parseIntValue(data, "vol");
    if (vol >= 0) {
      dfVolume = (uint8_t)constrain(vol, 0, 30);
      saveConfig();
      applyDfVolume();
    }
    if (isAudioReady && track > 0) {
      applyDfVolume();
      myDFPlayer.playMp3Folder(track);
    }
    Serial.println("OK");
    return;
  }

  if (line.startsWith("CONFIG:RANK_CONFIRM")) {
    confirmNextRank();
    Serial.println("OK");
    return;
  }

  if (line.startsWith("CONFIG:FUNC:")) {
    String data = line.substring(12);
    int flags = parseIntValue(data, "flags");
    if (flags >= 0) {
      cfgFuncFlags = (uint8_t)flags;
      saveConfig();
      Serial.println("OK");
      Serial.print("FUNC flags=");
      Serial.println(cfgFuncFlags);
    } else {
      Serial.println("ERR");
    }
    return;
  }

  if (line.startsWith("CONFIG:PRESET:")) {
    String data = line.substring(14);
    int v;
    v = parseIntValue(data, "maxhp");
    if (v >= 0)
      cfgMaxHP = v;
    v = parseIntValue(data, "starthp");
    if (v >= 0)
      cfgStartHP = v;
    v = parseIntValue(data, "maxrad");
    if (v >= 0)
      cfgMaxRad = v;
    v = parseIntValue(data, "money");
    if (v >= 0)
      cfgStartMoney = v;
    v = parseIntValue(data, "lvl");
    if (v >= 0)
      cfgStartLevel = v;
    v = parseIntValue(data, "xp");
    if (v >= 0)
      cfgStartXP = v;
    for (int i = 0; i < 8; i++) {
      String rkey = "r" + String(i);
      v = parseIntValue(data, rkey);
      if (v >= 0)
        cfgBaseProt[i] = v;
    }
    saveConfig();

    // Применить пресет к текущему состоянию
    playerMaxHP = cfgMaxHP;
    playerHP = cfgStartHP;
    playerMaxRad = cfgMaxRad;
    playerRad = 0;
    playerMoney = cfgStartMoney;
    playerLevel = cfgStartLevel;
    playerXP = cfgStartXP;
    for (int i = 0; i < 8; i++)
      innateRes[i] = cfgBaseProt[i];
    saveState();

    Serial.println("OK");
    Serial.print("PRESET maxhp=");
    Serial.print(cfgMaxHP);
    Serial.print(" starthp=");
    Serial.print(cfgStartHP);
    Serial.print(" maxrad=");
    Serial.print(cfgMaxRad);
    Serial.print(" money=");
    Serial.print(cfgStartMoney);
    Serial.print(" lvl=");
    Serial.print(cfgStartLevel);
    Serial.print(" xp=");
    Serial.println(cfgStartXP);
    return;
  }

  if (line.startsWith("CONFIG_READ")) {
    Serial.print("FUNC:flags=");
    Serial.println(cfgFuncFlags);
    Serial.print("PRESET:maxhp=");
    Serial.print(cfgMaxHP);
    Serial.print(",starthp=");
    Serial.print(cfgStartHP);
    Serial.print(",maxrad=");
    Serial.print(cfgMaxRad);
    Serial.print(",money=");
    Serial.print(cfgStartMoney);
    Serial.print(",lvl=");
    Serial.print(cfgStartLevel);
    Serial.print(",xp=");
    Serial.println(cfgStartXP);
    Serial.print("STATE:hp=");
    Serial.print(playerHP);
    Serial.print(",rad=");
    Serial.print(playerRad);
    Serial.print(",money=");
    Serial.print(playerMoney);
    Serial.print(",deaths=");
    Serial.println(playerDeaths);
    Serial.print("NAME:");
    Serial.println(playerName);
    {
      uint64_t mac = ESP.getEfuseMac();
      char buf[20];
      snprintf(buf, sizeof(buf), "%012llX", (unsigned long long)mac);
      Serial.print("UID:");
      Serial.println(buf);
    }
    return;
  }
}

// =====================================================
// ESP-NOW — ПРИЁМ ПАКЕТОВ
// =====================================================
volatile bool newDamageReceived = false;
volatile int16_t incomingDmgAmount = 0;
volatile int16_t incomingDmgMask = 0;
uint8_t anomalyMac[6];

volatile bool newRadReceived = false;
volatile int16_t incomingRadAmount = 0;

volatile bool newSafeZoneReceived = false;
volatile int16_t szHealAmount = 0;
volatile int16_t szRadAmount = 0;

uint32_t lastSafeZoneMs = 0;
int zoneProt[8] = {0};

bool isInSafeZone() {
  return lastSafeZoneMs > 0 && (millis() - lastSafeZoneMs < SZ_TIMEOUT_MS);
}

void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(Packet))
    return;

  Packet pkt;
  memcpy(&pkt, data, sizeof(Packet));

  if (pkt.msg_type == MSG_DAMAGE && pkt.val1 > 0) {
    // Проверяем: функция АНОМАЛИИ (бит 5) включена?
    if (!(cfgFuncFlags & (1 << 5)))
      return; // аномалии отключены
    noteAnomalySignal(info);
    incomingDmgAmount = pkt.val1;
    incomingDmgMask = pkt.val3;
    memcpy(anomalyMac, info->src_addr, 6);
    newDamageReceived = true;

  } else if (pkt.msg_type == MSG_RADIATION && pkt.val1 > 0) {
    if (!(cfgFuncFlags & (1 << 1)))
      return; // RAD отключена
    if (pkt.emitter == EMITTER_ANOMALY)
      noteAnomalySignal(info);
    incomingRadAmount = pkt.val1;
    newRadReceived = true;

  } else if (pkt.msg_type == MSG_SAFE_ZONE) {
    lastSafeZoneMs = millis();
    if (pkt.val3 == SZ_PROT_FLAG) {
      int t = (int)pkt.val1;
      if (t >= 0 && t <= 7)
        zoneProt[t] = (int)pkt.val2;
    } else if (pkt.val3 == SZ_BEACON_FLAG) {
      // Только обновление lastSafeZoneMs
    } else {
      szHealAmount = pkt.val1;
      szRadAmount = pkt.val2;
      newSafeZoneReceived = true;
    }
  }
}

// =====================================================
// ДИСПЛЕЙ + U8G2
// =====================================================
Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2;

int8_t currentPage = 0;
int8_t selectedRow = 0;
bool needFullRedraw = true;

char eventText[48] = "";
uint16_t eventColor = C_WHITE;
uint32_t eventTimeMs = 0;
uint32_t lastDamageMs = 0;

HardwareSerial mySerial2(2);
HardwareSerial bu03Serial(1);
DFRobotDFPlayerMini myDFPlayer;
bool isAudioReady = false;

// BU03 UWB (Serial1 G1/G2, PWR G42) — см. Test_PDA_Hardware / BU03_Distance_AT
#define BU03_POLL_MS 750
#define BU03_AT_TIMEOUT_MS 1500
#define BU03_RAW_MAX 96

enum Bu03LinkState : uint8_t {
  BU03_OFF = 0,
  BU03_NO_LINK,
  BU03_READY,
  BU03_NO_DATA,
  BU03_OK,
  BU03_ERROR
};

Bu03LinkState bu03Link = BU03_OFF;
float bu03DistM = -1.0f;
uint32_t bu03LastOkMs = 0;
uint32_t bu03LastPollMs = 0;
char bu03LastRaw[BU03_RAW_MAX] = "";
String bu03Rx;

// UI — 320×240, tft_panel.h (MADCTL 0x88, не Adafruit rotation)
#define SCR_W TFT_SCR_W
#define SCR_H TFT_SCR_H
#define EVT_Y 218
#define ROW_H 26

void idleSpiChipSelects() {
  pinMode(TFT_CS, OUTPUT);
  pinMode(LORA_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(LORA_CS, HIGH);
}

bool initDisplay() {
  idleSpiChipSelects();
  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, -1);
  tft.begin();
  tftApplyPanel(tft);
  tft.fillScreen(C_BLACK);
  u8g2.begin(tft);
  u8g2.setFontMode(1);
  u8g2.setFontDirection(0);
  u8g2.setFont(u8g2_font_8x13_t_cyrillic);
  return true;
}

void bu03Power(bool on) {
#if HAS_BU03_PWR
  pinMode(BU03_PWR, OUTPUT);
  digitalWrite(BU03_PWR, on ? HIGH : LOW);
  if (on)
    delay(2500);
#else
  (void)on;
#endif
}

void bu03Flush() {
  while (bu03Serial.available())
    (void)bu03Serial.read();
}

bool bu03WaitResponse(uint32_t ms, bool needDistance) {
  bu03Rx = "";
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    while (bu03Serial.available()) {
      char c = (char)bu03Serial.read();
      if (c == '\r')
        continue;
      if (bu03Rx.length() < 320)
        bu03Rx += c;
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
  return bu03WaitResponse(BU03_AT_TIMEOUT_MS, needDistance);
}

float bu03ParseDistance(const String &s) {
  int i = s.indexOf("distance:");
  if (i < 0)
    return -1.0f;
  return s.substring(i + 9).toFloat();
}

void bu03StoreRaw(const String &s) {
  size_t n = s.length();
  if (n >= BU03_RAW_MAX)
    n = BU03_RAW_MAX - 1;
  s.substring(0, n).toCharArray(bu03LastRaw, BU03_RAW_MAX);
}

const char *bu03StatusText() {
  switch (bu03Link) {
  case BU03_READY:
    return "ГОТОВ";
  case BU03_OK:
    return "OK";
  case BU03_NO_DATA:
    return "NO DATA";
  case BU03_ERROR:
    return "ERROR";
  case BU03_NO_LINK:
    return "NO LINK";
  default:
    return "OFF";
  }
}

uint16_t bu03StatusColor() {
  switch (bu03Link) {
  case BU03_OK:
    return C_GREEN;
  case BU03_READY:
    return C_CYAN;
  case BU03_NO_DATA:
    return C_YELLOW;
  case BU03_ERROR:
  case BU03_NO_LINK:
    return C_RED;
  default:
    return C_DGRAY;
  }
}

bool initBu03() {
  bu03Power(true);
  bu03Serial.setRxBufferSize(1024);
  bu03Serial.begin(115200, SERIAL_8N1, BU03_RX, BU03_TX);
  delay(400);
  bu03Flush();

  if (!bu03SendCmd("AT", false) || bu03Rx.indexOf("OK") < 0) {
    bu03Link = BU03_NO_LINK;
    bu03StoreRaw(bu03Rx);
    Serial.println("[BU03] NO LINK — check G1/G2, G42 PWR");
    return false;
  }

  bu03Link = BU03_READY;
  bu03StoreRaw(bu03Rx);
  Serial.println("[BU03] AT OK");
  return true;
}

bool pollBu03Distance() {
  if (bu03Link == BU03_OFF || bu03Link == BU03_NO_LINK)
    return false;

  if (!bu03SendCmd("AT+DISTANCE", true)) {
    bu03Link = BU03_ERROR;
    bu03StoreRaw(bu03Rx);
    return false;
  }

  bu03StoreRaw(bu03Rx);
  if (bu03Rx.indexOf("ERR") >= 0) {
    bu03Link = BU03_ERROR;
    return false;
  }

  float d = bu03ParseDistance(bu03Rx);
  if (d < 0.0f) {
    bu03Link = BU03_NO_DATA;
    return false;
  }

  bu03DistM = d;
  bu03LastOkMs = millis();
  bu03Link = BU03_OK;
  return true;
}

bool applyDfVolume() {
  if (!isAudioReady)
    return false;
  myDFPlayer.volume(dfVolume > 30 ? 30 : dfVolume);
  return true;
}

bool initDfPlayer() {
  while (mySerial2.available())
    (void)mySerial2.read();
  myDFPlayer.setTimeOut(DFPLAYER_CMD_TIMEOUT_MS);
  if (!myDFPlayer.begin(mySerial2, true, true))
    return false;
  if (myDFPlayer.readState() < 0)
    return false;
  applyDfVolume();
  return true;
}

void printRus(int x, int y, const char *text, uint16_t color) {
  u8g2.setForegroundColor(color);
  u8g2.setCursor(x, y + 13);
  u8g2.print(text);
}

void printRusStr(int x, int y, const String &text, uint16_t color) {
  u8g2.setForegroundColor(color);
  u8g2.setCursor(x, y + 13);
  u8g2.print(text);
}

void drawPageIndicator() {
  for (int i = 0; i < 5; i++) {
    int x = SCR_W - 62 + i * 12;
    int y = SCR_H - 12;
    if (i == currentPage)
      tft.fillCircle(x, y, 4, C_BORDER);
    else
      tft.drawCircle(x, y, 4, C_DGRAY);
  }
}

void drawBar(int x, int y, int w, int h, int val, int maxVal, uint16_t colHi,
             uint16_t colMid, uint16_t colLo) {
  tft.drawRect(x, y, w, h, C_BORDER);
  if (maxVal <= 0)
    return;
  int fw = (int)((long)val * (w - 2) / maxVal);
  if (fw < 0)
    fw = 0;
  if (fw > w - 2)
    fw = w - 2;
  float pct = (float)val / maxVal;
  uint16_t color = (pct > 0.5f) ? colHi : ((pct > 0.2f) ? colMid : colLo);
  if (fw > 0)
    tft.fillRect(x + 1, y + 1, fw, h - 2, color);
  if (fw < w - 2)
    tft.fillRect(x + 1 + fw, y + 1, w - 2 - fw, h - 2, C_BLACK);
}

void setEvent(const char *text, uint16_t color) {
  strncpy(eventText, text, 47);
  eventText[47] = '\0';
  eventColor = color;
  eventTimeMs = millis();
  needFullRedraw = true;
}

// ─── СТРАНИЦА 0: ГЛАВНАЯ ───
void drawPage0() {
  printRus(72, 8, "S.T.A.L.K.E.R. PDA", C_BORDER);
  if (isAnomalyDetectorActive())
    printRus(248, 8, "ДЕТ", C_ORANGE);
  tft.drawFastHLine(8, 28, SCR_W - 16, C_BORDER);

  int y = 40;

  if (cfgFuncFlags & (1 << 0)) {
    printRus(12, y, "HP", C_WHITE);
    drawBar(48, y - 2, 252, 18, playerHP, playerMaxHP, C_GREEN, C_YELLOW,
            C_RED);
    y += 32;
  }

  if (cfgFuncFlags & (1 << 1)) {
    printRus(12, y, "RAD", C_ORANGE);
    drawBar(48, y - 2, 252, 18, playerRad, playerMaxRad, C_ORANGE, C_ORANGE,
            C_ORANGE);
    y += 32;
  }

  String deathsTxt = "СМЕРТИ: " + String(playerDeaths);
  printRusStr(12, y, deathsTxt, C_RED);
  y += 28;

  if (cfgFuncFlags & (1 << 6)) {
    String lvlTxt = "LV" + String(playerLevel) + " " + String(getLevelTitle());
    printRusStr(12, y, lvlTxt, C_PURPLE);
    int nextXp = (playerLevel < MAX_LEVEL) ? xpTotalForLevel(playerLevel + 1) : playerXP;
    String xpTxt = "XP:" + String(playerXP) + "/" + String(nextXp);
    printRusStr(170, y, xpTxt, C_PURPLE);
    y += 28;
    if (playerRank + 1 < NUM_RANKS &&
        playerLevel >= RANK_TIERS[playerRank + 1].minLevel) {
      printRusStr(12, y, "Ранг: к мастеру!", C_YELLOW);
      y += 20;
    }
  }

  if (cfgFuncFlags & (1 << 2)) {
    String moneyTxt = String(playerMoney) + " RUB";
    printRusStr(12, y, moneyTxt, C_CYAN);
  }

  drawPageIndicator();
}

// ─── СТРАНИЦА 1: ИНВЕНТАРЬ ───
void drawPage1() {
  printRus(108, 8, "ИНВЕНТАРЬ", C_BORDER);
  tft.drawFastHLine(8, 28, SCR_W - 16, C_BORDER);

  if (cfgFuncFlags & (1 << 2)) {
    String balTxt = "БАЛАНС: " + String(playerMoney) + " RUB";
    printRusStr(12, 40, balTxt, C_CYAN);
  }
  tft.drawFastHLine(8, 58, SCR_W - 16, C_BORDER);

  const char *slotNames[] = {"БРОНЯ", "АРТ 1", "АРТ 2", "АРТ 3"};
  for (int i = 0; i < 4; i++) {
    int y = 68 + i * ROW_H;
    if (selectedRow == i) {
      tft.fillRect(8, y - 2, SCR_W - 16, ROW_H - 2, C_DGRAY);
      printRus(16, y, ">", C_WHITE);
      String rowTxt = String(slotNames[i]) + ":  [---]";
      printRusStr(32, y, rowTxt, C_WHITE);
    } else {
      tft.fillRect(8, y - 2, SCR_W - 16, ROW_H - 2, C_BLACK);
      String rowTxt = String(slotNames[i]) + ":  [---]";
      printRusStr(24, y, rowTxt, C_BORDER);
    }
  }

  drawPageIndicator();
}

// ─── СТРАНИЦА 2: СОПРОТИВЛЕНИЯ ───
void drawPage2() {
  printRus(84, 8, "СОПРОТИВЛЕНИЯ", C_BORDER);
  tft.drawFastHLine(8, 28, SCR_W - 16, C_BORDER);

  bool inZone = isInSafeZone();

  for (int i = 0; i < 7; i++) {
    int y = 40 + i * 22;
    tft.fillRect(8, y - 2, SCR_W - 16, 20, C_BLACK);
    printRus(12, y, DMG_NAMES[i], C_WHITE);

    String valTxt = String(innateRes[i]) + "%";
    uint16_t col = innateRes[i] > 0 ? C_GREEN : C_BORDER;
    printRusStr(180, y, valTxt, col);

    if (inZone && zoneProt[i] > 0) {
      String zoneTxt = "(+" + String(zoneProt[i]) + "%)";
      printRusStr(230, y, zoneTxt, C_GREEN);
    }
  }

  int y = 40 + 7 * 22;
  tft.fillRect(8, y - 2, SCR_W - 16, 20, C_BLACK);
  printRus(12, y, "RAD", C_ORANGE);
  String radTxt = String(innateRes[7]) + "%";
  printRusStr(180, y, radTxt, innateRes[7] > 0 ? C_ORANGE : C_BORDER);
  if (inZone && zoneProt[7] > 0) {
    String zoneTxt = "(+" + String(zoneProt[7]) + "%)";
    printRusStr(230, y, zoneTxt, C_ORANGE);
  }

  drawPageIndicator();
}

// ─── СТРАНИЦА 3: UWB / ДАЛЬНОСТЬ (тест BU03) ───
void drawPage3() {
  printRus(88, 8, "UWB / ДАЛЬНОСТЬ", C_BORDER);
  tft.drawFastHLine(8, 28, SCR_W - 16, C_BORDER);

  char pageLbl[16];
  snprintf(pageLbl, sizeof(pageLbl), "стр. %d/5", currentPage + 1);
  printRus(248, 8, pageLbl, C_DGRAY);

  tft.fillRect(8, 36, SCR_W - 16, 58, C_BLACK);
  printRus(12, 40, "СТАТУС:", C_WHITE);
  printRus(88, 40, bu03StatusText(), bu03StatusColor());

  printRus(12, 68, "ДИСТ:", C_WHITE);
  if (bu03Link == BU03_OK && bu03LastOkMs > 0) {
    char distBuf[16];
    snprintf(distBuf, sizeof(distBuf), "%.2f м", bu03DistM);
    printRus(72, 68, distBuf, C_GREEN);
    uint32_t age = millis() - bu03LastOkMs;
    char ageBuf[20];
    snprintf(ageBuf, sizeof(ageBuf), "(%lus)", (unsigned long)(age / 1000));
    printRusStr(168, 68, String(ageBuf), C_DGRAY);
  } else {
    printRus(72, 68, "---", C_DGRAY);
  }

  tft.drawFastHLine(8, 96, SCR_W - 16, C_BORDER);
  tft.fillRect(8, 100, SCR_W - 16, 60, C_BLACK);
  printRus(12, 104, "RAW AT:", C_DGRAY);
  if (bu03LastRaw[0]) {
    printRusStr(12, 124, String(bu03LastRaw), C_LGRAY);
  } else {
    printRus(12, 124, "(нет ответа)", C_DGRAY);
  }

  printRus(12, 168, "G1/G2 UART, G42 PWR", C_DGRAY);
  printRus(12, 188, "Нужен 2-й BU03 в эфире", C_DGRAY);

  drawPageIndicator();
}

// ─── СТРАНИЦА 4: ЗАДАНИЯ (активные + EEPROM CH1 квесты) ───
void drawPage4() {
  printRus(108, 8, "ЗАДАНИЯ", C_BORDER);
  tft.drawFastHLine(8, 28, SCR_W - 16, C_BORDER);
  if (activeTaskCount == 0) {
    printRus(72, 80, "НЕТ АКТИВНЫХ", C_DGRAY);
    printRus(24, 110, "Квест-чип CH1", C_DGRAY);
  } else {
    for (uint8_t i = 0; i < activeTaskCount && i < ACTIVE_TASKS_MAX; i++) {
      int y = 44 + i * 24;
      printRusStr(16, y, String(activeTasks[i].title), C_WHITE);
    }
    printRus(16, 200, "ОК = описание", C_DGRAY);
  }
  drawPageIndicator();
}

void drawScreen() {
  if (needFullRedraw) {
    tft.fillScreen(C_BLACK);
    needFullRedraw = false;
  }

  if (!playerRegistered) {
    tft.drawRect(4, 4, SCR_W - 8, SCR_H - 8, C_YELLOW);
    printRus(72, 72, "РЕГИСТРАЦИЯ", C_YELLOW);
    if (playerName[0])
      printRusStr(60, 110, String(playerName), C_WHITE);
    else
      printRus(48, 110, "ПОДКЛЮЧИТЕ ПК", C_LGRAY);
    return;
  }

  if (admitPending) {
    tft.drawRect(4, 4, SCR_W - 8, SCR_H - 8, C_RED);
    printRus(84, 72, "ОЖИДАНИЕ", C_RED);
    printRus(96, 100, "ДОПУСКА", C_RED);
    if (playerName[0])
      printRusStr(60, 128, String(playerName), C_LGRAY);
    else
      printRus(60, 128, "ДОПУСК В ИГРУ", C_LGRAY);
    return;
  }

  switch (currentPage) {
  case 0:
    drawPage0();
    break;
  case 1:
    drawPage1();
    break;
  case 2:
    drawPage2();
    break;
  case 3:
    drawPage3();
    break;
  case 4:
    drawPage4();
    break;
  }

  tft.drawFastHLine(8, EVT_Y - 6, SCR_W - 16, C_BORDER);
  if (eventText[0] && (millis() - eventTimeMs < 3500)) {
    tft.fillRect(8, EVT_Y - 4, SCR_W - 16, 18, C_BLACK);
    printRus(12, EVT_Y, eventText, eventColor);
  } else if (millis() - eventTimeMs >= 3500 && eventText[0]) {
    eventText[0] = '\0';
    tft.fillRect(8, EVT_Y - 4, SCR_W - 16, 18, C_BLACK);
  }

  if (playerDead) {
    tft.drawRect(4, 4, SCR_W - 8, SCR_H - 8, C_RED);
    printRus(60, 12, "СВЯЗЬ ПОТЕРЯНА", C_RED);
    printRus(72, 32, "ВОСКРЕШЕНИЕ", C_LGRAY);
  }
}

// =====================================================
// КНОПКИ (антидребезг, 4 штуки: DN / RT / OK / ESC)
// =====================================================
#define NUM_BTNS 4
const int BTN_PINS[NUM_BTNS] = {BTN_DN, BTN_RT, BTN_OK, BTN_ESC};
bool btnState[NUM_BTNS] = {HIGH, HIGH, HIGH, HIGH};
bool lastFlickerableState[NUM_BTNS] = {HIGH, HIGH, HIGH, HIGH};
uint32_t lastDebounceTime[NUM_BTNS] = {0, 0, 0, 0};
bool lastBtnState[NUM_BTNS] = {HIGH, HIGH, HIGH, HIGH};
uint32_t btnDownMs[NUM_BTNS] = {0};
bool btnLongFired[NUM_BTNS] = {false};
#define DEBOUNCE_DELAY 50
#define LONG_PRESS_MS 650

bool getDebouncedState(int pin, int idx) {
  bool reading = digitalRead(pin);
  if (reading != lastFlickerableState[idx]) {
    lastDebounceTime[idx] = millis();
  }
  if ((millis() - lastDebounceTime[idx]) > DEBOUNCE_DELAY) {
    if (reading != btnState[idx]) {
      btnState[idx] = reading;
    }
  }
  lastFlickerableState[idx] = reading;
  return btnState[idx];
}

// Проверяет нажатие (falling edge) для кнопки по индексу
bool btnPressed(int idx) {
  bool state = getDebouncedState(BTN_PINS[idx], idx);
  bool pressed = (state == LOW && lastBtnState[idx] == HIGH);
  lastBtnState[idx] = state;
  return pressed;
}

void adjustVolume(int delta) {
  int v = (int)dfVolume + delta;
  if (v < 0)
    v = 0;
  if (v > 30)
    v = 30;
  dfVolume = (uint8_t)v;
  applyDfVolume();
  saveConfig();
  char buf[24];
  snprintf(buf, sizeof(buf), "ГРОМКОСТЬ %u", (unsigned)dfVolume);
  setEvent(buf, C_YELLOW);
}

void handleButtons() {
  // Короткое DN/RT — навигация. Долгое DN/RT — громкость (пины 47/48 не используются).
  for (int idx = 0; idx < NUM_BTNS; idx++) {
    bool state = getDebouncedState(BTN_PINS[idx], idx);
    bool was = lastBtnState[idx];
    if (state == LOW && was == HIGH) {
      btnDownMs[idx] = millis();
      btnLongFired[idx] = false;
    }
    if (state == LOW && !btnLongFired[idx] &&
        (millis() - btnDownMs[idx]) >= LONG_PRESS_MS) {
      btnLongFired[idx] = true;
      if (idx == 0)
        adjustVolume(-2);
      else if (idx == 1)
        adjustVolume(2);
    }
    bool released = (state == HIGH && was == LOW);
    lastBtnState[idx] = state;
    if (!released || btnLongFired[idx])
      continue;
    if (idx == 0) {
      if (currentPage == 1)
        selectedRow = (selectedRow + 1) % 4;
      needFullRedraw = true;
    } else if (idx == 1) {
      currentPage = (currentPage + 1) % 5;
      needFullRedraw = true;
    } else if (idx == 2) {
      needFullRedraw = true;
    } else if (idx == 3) {
      currentPage = (currentPage + 4) % 5;
      needFullRedraw = true;
    }
  }
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);

  for (int i = 0; i < NUM_BTNS; i++)
    pinMode(BTN_PINS[i], INPUT_PULLUP);

  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(PIN_VIBRO, OUTPUT);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_RED, LOW);
  digitalWrite(PIN_VIBRO, LOW);

  bu03Power(false);
  pinMode(LORA_DIO0, INPUT);
  idleSpiChipSelects();

  initDisplay();

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(100000);

  // NVS — сначала конфиг, потом состояние (конфиг нужен для дефолтов state)
  loadConfig();
  loadState();
  admitPending = true;  // каждое включение — блокировка системы до допуска мастера

  // Сплеш (320×240)
  printRus(72, 72, "S.T.A.L.K.E.R.", C_GREEN);
  printRus(108, 100, "PDA v2.7", C_LGRAY);

  delay(500);
  printRus(108, 128, "Wi-Fi...", C_CYAN);

  WiFi.mode(WIFI_STA);
  WiFi.setChannel(1);
  WiFi.disconnect();

  delay(200);
  if (esp_now_init() != ESP_OK) {
    printRus(72, 156, "ESP-NOW ОШИБКА!", C_RED);
  } else {
    esp_now_register_recv_cb(OnDataRecv);
    printRus(96, 156, "ESP-NOW OK", C_CYAN);
  }

  delay(500);

  // DFPlayer (Serial2; needs 5V BUS — probe, skip if no module)
  mySerial2.begin(9600, SERIAL_8N1, DFPLAYER_RX, DFPLAYER_TX);
  if (initDfPlayer()) {
    isAudioReady = true;
    printRus(108, 184, "АУДИО OK", C_GREEN);
    myDFPlayer.playMp3Folder(3);
  } else {
    printRus(72, 184, "АУДИО: нет 5V?", C_RED);
  }

  printRus(96, 208, "BU03...", C_CYAN);
  if (initBu03()) {
    printRus(108, 228, "UWB OK", C_GREEN);
  } else {
    printRus(72, 228, "UWB: нет связи", C_RED);
  }

  delay(2000);
  tft.fillScreen(C_BLACK);

  sessionStartMs = millis();
  lastDeathAtLevelUp = playerDeaths;
  bu03LastPollMs = millis();
  Serial.println("PDA v2.7 READY");
  Serial.print("FUNC flags=");
  Serial.println(cfgFuncFlags);
  Serial.print("maxHP=");
  Serial.print(cfgMaxHP);
  Serial.print(" HP=");
  Serial.println(playerHP);

  needFullRedraw = true;
}

// =====================================================
// LOOP
// =====================================================
uint32_t lastDrawMs = 0;

void loop() {
  handleButtons();

  // ─── Serial: приём CONFIG от programmer.py ───
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      handleSerialConfig(line);
    }
  }

  // ─── Обработка DAMAGE (аномалии) ───
  if (newDamageReceived) {
    newDamageReceived = false;
    if (admitPending || playerDead) {
      // Бой заблокирован: нет допуска или смерть в игре
    } else {
    lastDamageMs = millis();
    noteAnomalyDiscovery(anomalyMac);

    if (cfgFuncFlags & (1 << 0)) { // HP система включена
      int dmg = incomingDmgAmount;
      uint16_t mask = (uint16_t)incomingDmgMask;
      int res = 0;
      for (int b = 0; b < 7; b++) {
        if (mask & (1 << b)) {
          int r = innateRes[b];
          if (isInSafeZone())
            r += zoneProt[b];
          res = max(res, min(100, r));
        }
      }
      int actualDmg = dmg * (100 - res) / 100;
      if (actualDmg < 0)
        actualDmg = 0;

      playerHP -= actualDmg;
      if (playerHP <= 0) {
        playerHP = 0;
        playerDead = true;
        playerDeaths++;
      }
      saveState();

      if (isAudioReady)
        myDFPlayer.playMp3Folder(1);

      char buf[30];
      snprintf(buf, sizeof(buf), "УРОН: -%d HP", actualDmg);
      setEvent(buf, C_RED);
      needFullRedraw = true;
    }

    // ACK (всегда отправляем, даже если HP отключён)
    Packet ackPkt;
    ackPkt.emitter = EMITTER_PLAYER;
    ackPkt.msg_type = MSG_ACK;
    ackPkt.val1 = playerHP;
    ackPkt.val2 = 0;
    ackPkt.val3 = 0;
    if (!esp_now_is_peer_exist(anomalyMac)) {
      esp_now_peer_info_t peer = {};
      memcpy(peer.peer_addr, anomalyMac, 6);
      peer.channel = 1;
      esp_now_add_peer(&peer);
    }
    esp_now_send(anomalyMac, (uint8_t *)&ackPkt, sizeof(Packet));
    }
  }

  // ─── Обработка RADIATION ───
  if (newRadReceived) {
    newRadReceived = false;
    if (!admitPending && !playerDead && (cfgFuncFlags & (1 << 1))) { // RAD функция включена
      int radRes = innateRes[7];
      if (isInSafeZone())
        radRes += zoneProt[7];
      radRes = min(100, radRes);
      int actualRad = incomingRadAmount * (100 - radRes) / 100;
      if (actualRad < 0)
        actualRad = 0;
      playerRad += actualRad;
      if (playerRad > playerMaxRad)
        playerRad = playerMaxRad;
      saveState();
      char rbuf[24];
      snprintf(rbuf, sizeof(rbuf), "RAD: +%d", actualRad);
      setEvent(rbuf, C_ORANGE);
      needFullRedraw = true;
    }
  }

  // ─── Обработка Safe Zone (лечение) ───
  if (newSafeZoneReceived) {
    newSafeZoneReceived = false;
    if (admitPending || playerDead) {
      lastSafeZoneMs = millis();
    } else {
    lastSafeZoneMs = millis();

    if (cfgFuncFlags & (1 << 0)) { // HP включена
      if (playerHP > 0 && playerHP < playerMaxHP && szHealAmount > 0) {
        playerHP += szHealAmount;
        if (playerHP > playerMaxHP)
          playerHP = playerMaxHP;
      }
    }
    if (cfgFuncFlags & (1 << 1)) { // RAD включена
      if (szRadAmount > 0 && playerRad > 0) {
        playerRad -= szRadAmount;
        if (playerRad < 0)
          playerRad = 0;
      }
    }
    saveState();

    if (szHealAmount > 0 || szRadAmount > 0) {
      char buf[32];
      snprintf(buf, sizeof(buf), "ЗЗ: +%d HP -%d RAD", szHealAmount,
               szRadAmount);
      setEvent(buf, C_GREEN);
    } else {
      setEvent("ЗЕЛЕНАЯ ЗОНА", C_GREEN);
    }
    needFullRedraw = true;
    }
  }

  // ─── LED и вибро (batch 2: зелёный=ЗЗ постоянно; красный=вспышка урона;
  //     мигание красного при детекторе+аномалия — TODO Фаза 5) ───
  digitalWrite(LED_GREEN, isInSafeZone() ? HIGH : LOW);

  // Красный LED: вспышка в момент урона (~1 с)
  if (lastDamageMs > 0 && (millis() - lastDamageMs < 1000)) {
    digitalWrite(LED_RED, HIGH);
  } else {
    digitalWrite(LED_RED, LOW);
  }

  // Вибро: урон (250 мс); достижение/ранг — отдельный таймер.
  // TODO Фаза 5: вибро на все типы setEvent при VIB ВКЛ (см. MASTER_SPEC §10)
  if ((lastDamageMs > 0 && millis() - lastDamageMs < 250) ||
      (achVibroUntilMs > 0 && millis() < achVibroUntilMs)) {
    digitalWrite(PIN_VIBRO, HIGH);
  } else {
    digitalWrite(PIN_VIBRO, LOW);
  }

  // ─── Выживание 2 ч без смерти (достижение) ───
  if ((cfgFuncFlags & (1 << 6)) && !hasAchievement(ACH_SURVIVE_2H) &&
      playerDeaths == lastDeathAtLevelUp && sessionStartMs > 0 &&
      (millis() - sessionStartMs >= 7200000UL)) {
    grantAchievement(ACH_SURVIVE_2H);
  }

  // ─── EEPROM CH1: терминалы (магазин/банк/квест/допуск) ~200 ms ───
  if (millis() - lastTxnPollMs >= TXN_POLL_MS) {
    lastTxnPollMs = millis();
    pollEepromTransaction();
  }

  // ─── BU03: опрос дистанции (~750 ms) ───
  if (bu03Link != BU03_OFF && bu03Link != BU03_NO_LINK &&
      millis() - bu03LastPollMs >= BU03_POLL_MS) {
    bu03LastPollMs = millis();
    pollBu03Distance();
    if (currentPage == 3 || currentPage == 4)
      needFullRedraw = true;
  }

  // ─── Перерисовка экрана (5 FPS) ───
  if (millis() - lastDrawMs >= 200) {
    lastDrawMs = millis();
    drawScreen();
  }
}
