/**
 * ╔══════════════════════════════════════════════════╗
 * ║     S.T.A.L.K.E.R. ПДА — Прошивка ESP32 v2.8   ║
 * ║       Плата: ESP32-S3-N16R8 (DevKitC-1)         ║
 * ╠══════════════════════════════════════════════════╣
 * ║ CONFIG:FUNC / CONFIG:PRESET через USB Serial     ║
 * ║ Протокол: protocol.py v4.2                       ║
 * ║ Пины: MASTER_SPECIFICATION.md §3 (июнь 2026)     ║
 * ╠══════════════════════════════════════════════════╣
 * ║ Миграция v2.0→v2.1 (аппаратная валидация):       ║
 * ║ • M024 320×240 — tft_panel.h MADCTL 0x88 (v2.8)  ║
 * ║ • 4 кнопки DN/RT/OK/ESC (G4–G7)                      ║
 * ║ • G47/G48 не используются; ключа SND нет             ║
 * ║ • Громкость — страница «НАСТРОЙКИ» (не жесты)        ║
 * ║ • LED G15/G16, вибро G21, DFPlayer Serial2           ║
 * ║ • BU03 Serial1 G1/G2, BU03_PWR G42                   ║
 * ║ • LoRa SPI G39/G40/G41, RST не пульсировать (G12=TFT) ║
 * ║ • Питание ПДА — ключ PWR; DFPlayer вместе с ПДА      ║
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
 * ║ • Агония: после HP=0 (не 10%); аптечка/ЗЗ/heal     ║
 * ║   или повторный удар / сдаться. Зомби: только       ║
 * ║   сдаться; входящие TXN без notify, кроме broadcast ║
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
#include "chip_header.h"
#include "achievements.h"
#include "quest_catalog.h"

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
#define LORA_DIO0 41
#define LORA_SCK TFT_SCK
#define LORA_MISO TFT_MISO
#define LORA_MOSI TFT_MOSI

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

#include "zone_msgs.h"
#include "lora_link.h"

// Emitter (protocol.py)
#define EMITTER_SYSTEM 0
#define EMITTER_PLAYER 1
#define EMITTER_ANOMALY 2
#define EMITTER_BASE 3

// Msg (protocol.py)
#define MSG_DAMAGE 1
#define MSG_HEAL 2
#define MSG_RADIATION 3
#define MSG_COMMAND 5
#define MSG_ACK 6
#define MSG_SAFE_ZONE 7
#define MSG_EMISSION 8
#define MSG_RADIO 11
// MSG_ZONE_* — zone_msgs.h

#define CMD_KILL 0
#define CMD_REVIVE 1
#define CMD_WIPE 2
#define CMD_ADD_MONEY 3
#define CMD_SET_PROJECT 4
#define CMD_ADD_XP 5
#define CMD_GRANT_RESISTANCE 6
#define CMD_ADMIT 7

#define EMISSION_DMG_TICK 250
#define EMISSION_RAD_TICK 250
#define RAD_SICKNESS_HP 10
#define ROLE_STALKER 0
#define ROLE_CONTROLLER 1
#define ROLE_MUTANT 2
#define ANTIPHASE_TIMEOUT_MS 3500
#define RSSI_SAFE_EXIT (-85)
#define RESISTANCE_THRESHOLD 1000
#define RESISTANCE_CAP 50
#define IFRAME_MS 900
#define CHIP_SAVE_ADDR 0x0100
#define MENU_COUNT 6

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
bool playerAgony = false;
bool playerZombie = false;
bool admitPending = true;
int cheatShieldCount = 0;
int8_t actorRole = ROLE_STALKER;
int emissionTimer = -1;
int emissionDuration = 0;
int emissionStrikeTick = 0;
bool szEmissionProtect = false;
int dmgAcc[8] = {0};
int totalHpDamage = 0;
int radTickCounter = 0;
int antiradUses = 0;
int totalSpent = 0;
int totalEarned = 0;
int questsDone = 0;
int hiddenQuestsDone = 0;
int zzHealTotal = 0;
int detectorTriggers = 0;
int arenaFights = 0;
int arenaWins = 0;
int bankVolume = 0;
uint32_t lastAnomalyPacketMs = 0;
bool inAnomalyZone = false;
int shieldBreaks = 0;
uint8_t menuIndex = 0;
int8_t menuSub = -1;
uint8_t surrenderStep = 0;
bool chipConfirmPending = false;
ChipHeader pendingChip;
char pendingChipName[25] = "";
uint8_t pendingChipCh = 0;
uint32_t lastGameTickMs = 0;
uint8_t achScroll = 0;
uint8_t questSel = 0;
uint8_t notifyIdx = 0;
char notifyLog[5][48];
uint8_t notifyCount = 0;
bool hadRadHigh = false;
uint32_t lastIframeMs = 0;
uint8_t lastIframeMac[6] = {0};
volatile bool newEmissionReceived = false;
volatile int16_t incomingEmissionTimer = 0;
volatile int16_t incomingEmissionDur = 0;
volatile bool newCommandReceived = false;
volatile int16_t incomingCmd = 0;
volatile int16_t incomingCmdVal2 = 0;
volatile bool newRadioReceived = false;
volatile int16_t incomingRadioTrack = 0;
volatile int16_t incomingRadioVol = 0;
volatile bool incomingFromPlayer = false;
volatile bool incomingControllerPsi = false;
uint8_t anomalyMac[6] = {0};
bool loraOk = false;
int zoneSlot = -1;
uint8_t zoneAnomMac[6] = {0};
volatile bool pendingZoneAssign = false;
volatile int pendingZoneSlot = -1;
uint8_t pendingZoneMac[6] = {0};
uint32_t lastZoneHelloMs = 0;
uint32_t lastZoneAssignMs = 0;
int16_t szRadiusM = 10;
bool uwbInShelter = false;
bool wantShelterUwb = false;
uint8_t shelterMac[6] = {0};
uint32_t lastShelterUwbSetMs = 0;
uint32_t lastEntryOkMs = 0;
bool bu03Present = false;
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
void setEvent(const char *text, uint16_t color, bool admin = false);
void markAgony();
void clearAgony();
void checkLevelUps();
void checkRankReadyNotify();
void saveState();
void completeRegistration(const char *name);
void pollQuestBoard();
bool writeBoardTake(const char *qid, bool complete);
bool applyDfVolume();
bool isInSafeZone();
void markDead();
void markZombie();
void doSurrender();
void startEmission(int timerSec, int durSec);
void gameTick();
void pulseVibro(uint16_t ms);
void refreshLoadoutAchievements();
void grantQuestMilestones();
void grantSpendMilestones();
void grantEarnMilestones();
extern Preferences prefs;
extern bool needFullRedraw;
extern bool isAudioReady;
extern DFRobotDFPlayerMini myDFPlayer;

#define MAX_LEVEL 100
#define REGISTRATION_LEVEL 2
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

int playerRank = 0;
uint32_t achFlags[3] = {0, 0, 0};
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
int8_t selectedRow = 0;

QuestCatRec boardQuests[QUEST_CAT_MAX];
uint8_t boardQuestCount = 0;
uint8_t boardVisibleIdx[QUEST_CAT_MAX];
uint8_t boardVisibleCount = 0;
uint8_t boardSel = 0;
bool boardOverlay = false;
bool boardOverlayDismissed = false;
bool boardTakePending = false;
uint32_t boardTakeAtMs = 0;
uint16_t boardCatCrc = 0;
bool boardWasPresent = false;

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

bool hasAchievement(uint8_t id) {
  if (id >= ACH_COUNT)
    return false;
  return (achFlags[id / 32] & (1UL << (id % 32))) != 0;
}

void pulseVibro(uint16_t ms) {
  if (playerZombie)
    return;
  achVibroUntilMs = millis() + ms;
}

void grantAchievement(uint8_t id) {
  if (id >= ACH_COUNT || hasAchievement(id))
    return;
  if (!(cfgFuncFlags & (1 << 6)))
    return;
  achFlags[id / 32] |= (1UL << (id % 32));
  const AchDef &a = ACH_TABLE[id];
  playerXP += a.xp;
  playerMoney += a.rub;
  if (!playerZombie) {
    char buf[56];
    snprintf(buf, sizeof(buf), "ДОСТИЖЕНИЕ: %s +%dXP +%dRUB", a.name, a.xp,
             a.rub);
    setEvent(buf, C_PURPLE);
    pulseVibro(350);
  }
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
  if (playerZombie)
    return;
  char buf[40];
  snprintf(buf, sizeof(buf), "РАНГ ДОСТУПЕН: %s", RANK_TIERS[next].title);
  setEvent(buf, C_YELLOW);
  pulseVibro(500);
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
    pulseVibro(400);
    if (playerLevel == 2) grantAchievement(ACH_LEVEL_2);
    if (playerLevel == 10) grantAchievement(ACH_LEVEL_10);
    if (playerLevel == 25) grantAchievement(ACH_LEVEL_25);
    if (playerLevel == 50) grantAchievement(ACH_LEVEL_50);
    if (playerLevel == 75) grantAchievement(ACH_LEVEL_75);
    if (playerLevel == 100) grantAchievement(ACH_LEVEL_100);
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
  if (!hasAchievement(ACH_ANOMALY_1))
    grantAchievement(ACH_ANOMALY_1);
  else {
    playerXP += 40;
    checkLevelUps();
  }
  saveState();
}

bool canUseStore() { return playerLevel >= LVL_STORE; }
bool canUseAtm() { return playerLevel >= LVL_ATM; }
bool canUseArmor() { return playerLevel >= LVL_ARMOR; }
bool canUseArtifact1() { return playerLevel >= LVL_ARTIFACT1; }
bool canUseHiddenQuest() { return playerLevel >= LVL_HIDDEN_QUEST; }
bool canUseDetector() { return playerLevel >= LVL_DETECTOR; }

int8_t lastAnomalyRssi = -100;
uint32_t lastAnomalySignalMs = 0;

void noteAnomalySignal(const esp_now_recv_info_t *info) {
  if (info && info->rx_ctrl)
    lastAnomalyRssi = info->rx_ctrl->rssi;
  lastAnomalySignalMs = millis();
  lastAnomalyPacketMs = millis();
  inAnomalyZone = true;
}

bool isAnomalyDetectorActive() {
  if (!canUseDetector() || lastAnomalySignalMs == 0)
    return false;
  return (millis() - lastAnomalySignalMs) < DETECTOR_SIGNAL_TIMEOUT_MS;
}
bool canUseGlobalMsg() { return playerLevel >= LVL_GLOBAL_MSG; }

// =====================================================
// EEPROM — терминалы↔ПДА транзакции (CH0 universal, блок @0x80)
// и applyChip() на CH0 + слоты CH2/CH3/CH5/CH6
// =====================================================
#define EEPROM_DEV EEPROM_ADDR_DEFAULT
#define UNIVERSAL_SLOT MUX_CH_UNIVERSAL
#define EEPROM_WR_DLY 5
#define CHIP_HOTPLUG_MS 100
#define EQ_SLOT_COUNT 4
static const uint8_t EQ_SLOT_MUX_CH[EQ_SLOT_COUNT] = {
    MUX_CH_SLOT_1, MUX_CH_SLOT_2, MUX_CH_SLOT_3, MUX_CH_SLOT_4
};
static const char *EQ_SLOT_LABEL[EQ_SLOT_COUNT] = {"CH2", "CH3", "CH5", "CH6"};

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

bool eepromWriteByteCh(uint8_t ch, uint16_t addr, uint8_t val) {
  if (!tcaSelect(ch))
    return false;
  Wire.beginTransmission(EEPROM_DEV);
  Wire.write((uint8_t)(addr >> 8));
  Wire.write((uint8_t)(addr & 0xFF));
  Wire.write(val);
  uint8_t err = Wire.endTransmission();
  delay(EEPROM_WR_DLY);
  return err == 0;
}

bool eepromReadByteCh(uint8_t ch, uint16_t addr, uint8_t &val) {
  if (!tcaSelect(ch))
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

bool eepromReadBlockCh(uint8_t ch, uint16_t addr, uint8_t *buf, uint8_t len) {
  for (uint8_t i = 0; i < len; i++) {
    if (!eepromReadByteCh(ch, addr + i, buf[i]))
      return false;
  }
  return true;
}

bool eepromWriteBlockCh(uint8_t ch, uint16_t addr, const uint8_t *data,
                        uint8_t len) {
  for (uint8_t i = 0; i < len; i++) {
    if (!eepromWriteByteCh(ch, addr + i, data[i]))
      return false;
  }
  return true;
}

bool chipPresentOnCh(uint8_t ch) {
  if (!tcaSelect(ch))
    return false;
  Wire.beginTransmission(EEPROM_DEV);
  return Wire.endTransmission() == 0;
}

bool eepromWriteByteChUniversal(uint16_t addr, uint8_t val) {
  return eepromWriteByteCh(UNIVERSAL_SLOT, addr, val);
}

bool eepromReadByteChUniversal(uint16_t addr, uint8_t &val) {
  return eepromReadByteCh(UNIVERSAL_SLOT, addr, val);
}

bool eepromReadBlockChUniversal(uint16_t addr, uint8_t *buf, uint8_t len) {
  return eepromReadBlockCh(UNIVERSAL_SLOT, addr, buf, len);
}

bool eepromWriteBlockChUniversal(uint16_t addr, const uint8_t *data,
                                 uint8_t len) {
  return eepromWriteBlockCh(UNIVERSAL_SLOT, addr, data, len);
}

bool chipPresentOnChUniversal() { return chipPresentOnCh(UNIVERSAL_SLOT); }

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
  if (admitPending || playerDead || playerZombie || playerAgony) {
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
  totalSpent += o.paid;
  grantSpendMilestones();
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
  bool deposit = (flags & TXN_FLAG_BANK_DEPOSIT) != 0;
  if (admitPending || playerDead || playerAgony ||
      (playerZombie && deposit)) {
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
    bankVolume += amount;
    grantAchievement(ACH_TRANSFER_FIRST);
    if (bankVolume >= 10000)
      grantAchievement(ACH_BANK_10K);
    if (amount >= 5000)
      grantAchievement(ACH_TRANSFER_5K);
    saveState();
    snprintf(o.eventMsg, sizeof(o.eventMsg), "ВКЛАД: -%d RUB", (int)amount);
    o.eventColor = C_CYAN;
  } else {
    playerMoney += amount;
    o.paid = amount;
    o.balanceAfter = playerMoney;
    o.result = TXN_RESULT_OK;
    bankVolume += amount;
    grantAchievement(ACH_TRANSFER_FIRST);
    if (bankVolume >= 10000)
      grantAchievement(ACH_BANK_10K);
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
  boardTakePending = false;
  o.flagsOut = flags;
  char qid[12];
  memset(qid, 0, sizeof(qid));
  if (questIdPrefix[0]) {
    strncpy(qid, questIdPrefix, 8);
    qid[8] = '\0';
  } else {
    snprintf(qid, sizeof(qid), "q%u", (unsigned)questCatId);
  }

  if (admitPending || playerDead || playerAgony) {
    txnFailLocked(o);
    return;
  }

  bool complete = (flags & TXN_FLAG_QUEST_COMPLETE) != 0;
  if (!complete && (flags & TXN_FLAG_QUEST_HIDDEN) && !canUseHiddenQuest()) {
    txnFailLevel(o, "СКРЫТЫЙ КВЕСТ");
    return;
  }
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
    questsDone++;
    if (flags & TXN_FLAG_QUEST_HIDDEN)
      hiddenQuestsDone++;
    grantQuestMilestones();
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
  t.id[sizeof(t.id) - 1] = '\0';
  const char *btitle = nullptr;
  for (uint8_t i = 0; i < boardQuestCount; i++) {
    if (strncmp(boardQuests[i].id, qid, 8) == 0) {
      btitle = boardQuests[i].title;
      break;
    }
  }
  if (btitle && btitle[0])
    strncpy(t.title, btitle, sizeof(t.title) - 1);
  else
    snprintf(t.title, sizeof(t.title), "Задание #%u", (unsigned)questCatId);
  t.title[sizeof(t.title) - 1] = '\0';
  strncpy(t.shortDesc, "Доска заданий", sizeof(t.shortDesc) - 1);
  t.shortDesc[sizeof(t.shortDesc) - 1] = '\0';
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
  grantAchievement(ACH_START_FIRST);
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

void rebuildBoardVisible() {
  boardVisibleCount = 0;
  for (uint8_t i = 0; i < boardQuestCount && boardVisibleCount < QUEST_CAT_MAX; i++) {
    if (boardQuests[i].hidden && !canUseHiddenQuest())
      continue;
    if (!canUseStore() && !boardQuests[i].hidden)
      continue;
    boardVisibleIdx[boardVisibleCount++] = i;
  }
  if (boardSel >= boardVisibleCount)
    boardSel = 0;
}

bool writeBoardTake(const char *qid, bool complete) {
  if (!chipPresentOnChUniversal() || ch0TxnBusy())
    return false;
  uint8_t buf[QUEST_TAKE_SIZE];
  const char *uid = playerAssignedUid[0] ? playerAssignedUid : "anon";
  quest_take_pack(buf, qid, uid, complete ? QUEST_TAKE_COMPLETE : 0);
  if (!eepromWriteBlockChUniversal(QUEST_TAKE_BASE, buf, QUEST_TAKE_SIZE))
    return false;
  boardTakePending = true;
  boardTakeAtMs = millis();
  return true;
}

void clearBoardState() {
  boardQuestCount = 0;
  boardVisibleCount = 0;
  boardOverlay = false;
  boardOverlayDismissed = false;
  boardTakePending = false;
  boardCatCrc = 0;
  boardSel = 0;
}

void pollQuestBoard() {
  if (playerZombie || playerDead) {
    if (boardWasPresent)
      clearBoardState();
    boardWasPresent = false;
    return;
  }
  if (!chipPresentOnChUniversal()) {
    if (boardWasPresent)
      clearBoardState();
    boardWasPresent = false;
    return;
  }
  boardWasPresent = true;

  if (boardTakePending) {
    uint8_t tq[QUEST_TAKE_SIZE];
    bool tqGone = true;
    if (eepromReadBlockChUniversal(QUEST_TAKE_BASE, tq, QUEST_TAKE_SIZE) &&
        tq[0] == QUEST_TAKE_MAGIC0 && tq[1] == QUEST_TAKE_MAGIC1)
      tqGone = false;
    if (tqGone && !ch0TxnBusy()) {
      if (millis() - boardTakeAtMs > 2500) {
        boardTakePending = false;
        setEvent("ДОСКА: ЗАНЯТО", C_ORANGE);
        needFullRedraw = true;
      }
    }
    if (ch0TxnBusy() || (millis() - boardTakeAtMs < 4000 && !tqGone)) {
      /* wait for TXN */
    } else if (tqGone) {
      boardTakePending = false;
    }
  }

  uint8_t hdr[QUEST_CAT_HDR_SIZE];
  if (!eepromReadBlockChUniversal(QUEST_CAT_BASE, hdr, QUEST_CAT_HDR_SIZE))
    return;
  uint8_t n = 0;
  uint16_t crc = 0;
  if (!quest_cat_parse_hdr(hdr, &n, &crc)) {
    if (boardQuestCount) {
      boardQuestCount = 0;
      boardVisibleCount = 0;
      boardOverlay = false;
      needFullRedraw = true;
    }
    return;
  }
  if (n > QUEST_CAT_MAX)
    n = QUEST_CAT_MAX;
  if (crc == boardCatCrc && n == boardQuestCount)
    return;

  uint8_t rec[QUEST_CAT_REC_SIZE];
  uint8_t loaded = 0;
  for (uint8_t i = 0; i < n; i++) {
    if (!eepromReadBlockChUniversal(
            QUEST_CAT_BASE + QUEST_CAT_HDR_SIZE + i * QUEST_CAT_REC_SIZE, rec,
            QUEST_CAT_REC_SIZE))
      return;
    quest_cat_parse_rec(rec, &boardQuests[loaded]);
    if (boardQuests[loaded].id[0])
      loaded++;
  }
  boardQuestCount = loaded;
  boardCatCrc = crc;
  rebuildBoardVisible();
  if (!boardOverlayDismissed && boardVisibleCount > 0 && !admitPending &&
      !playerAgony)
    boardOverlay = true;
  needFullRedraw = true;
}

bool ch0TxnBusy() {
  uint8_t block[TXN_BLOCK_SIZE];
  if (!eepromReadBlockChUniversal(TXN_EEPROM_BASE, block, TXN_BLOCK_SIZE))
    return false;
  if (!txn_validate_block(block))
    return false;
  uint8_t st = txn_get_state(block);
  return st == TXN_STATE_PENDING || st == TXN_STATE_PROCESSING;
}

struct ChipSlotRt {
  bool present;
  bool settled;
  bool applied;
  bool rejected;
  uint8_t type;
  uint8_t sub;
  int16_t params[16];
  char label[12];
  uint32_t detectMs;
  uint32_t lastRegenMs;
};

ChipSlotRt ch0Rt = {};
ChipSlotRt eqRt[EQ_SLOT_COUNT] = {};

int equipProt[8] = {0, 0, 0, 0, 0, 0, 0, 0};
int stimProt[8] = {0, 0, 0, 0, 0, 0, 0, 0};
uint32_t stimUntilMs = 0;
uint32_t immuneUntilMs = 0;
int consRegenHp = 0;
uint32_t consRegenHpUntilMs = 0;
uint32_t consRegenHpLastMs = 0;
int consRegenRad = 0;
uint32_t consRegenRadUntilMs = 0;
uint32_t consRegenRadLastMs = 0;

const char *chipTypeLabel(uint8_t type, uint8_t sub) {
  if (type == CHIP_TYPE_ARMOR)
    return "БРОНЯ";
  if (type == CHIP_TYPE_ARTIFACT)
    return "АРТ";
  if (type == CHIP_TYPE_ADMIN)
    return "АДМИН";
  if (type == CHIP_TYPE_QUEST)
    return "КВЕСТ";
  if (type == CHIP_TYPE_SHOP)
    return "МАГАЗИН";
  if (type == CHIP_TYPE_CONSUMABLE) {
    switch (sub) {
    case CHIP_SUB_HEAL:
      return "АПТЕЧКА";
    case CHIP_SUB_ANTIRAD:
      return "АНТИРАД";
    case CHIP_SUB_REGEN:
      return "РЕГЕН";
    case CHIP_SUB_STIM:
      return "СТИМ";
    case CHIP_SUB_RESTORE:
      return "РЕМОНТ";
    case CHIP_SUB_UPGRADE:
      return "УЛУЧШ.";
    default:
      return "РАСХОД";
    }
  }
  return "ЧИП";
}

void clampHP() {
  if (playerMaxHP < 1)
    playerMaxHP = 1;
  if (playerHP > playerMaxHP)
    playerHP = playerMaxHP;
  if (playerHP < 0)
    playerHP = 0;
}

int protForType(int idx) {
  int r = innateRes[idx] + equipProt[idx];
  if (stimUntilMs && millis() < stimUntilMs)
    r += stimProt[idx];
  if (r > 100)
    r = 100;
  if (r < 0)
    r = 0;
  return r;
}

int countEquippedType(uint8_t type) {
  int n = 0;
  for (uint8_t i = 0; i < EQ_SLOT_COUNT; i++) {
    if (eqRt[i].applied && eqRt[i].type == type)
      n++;
  }
  return n;
}

bool readChipHeaderCh(uint8_t ch, ChipHeader &hdr, char *nameBuf, uint8_t nameLen) {
  uint8_t buf[CHIP_HEADER_SIZE];
  if (!eepromReadBlockCh(ch, 0, buf, CHIP_HEADER_SIZE))
    return false;
  if (!chip_parse(buf, &hdr))
    return false;
  if (nameBuf && nameLen) {
    uint8_t ext[CHIP_NAME_MAX];
    memset(nameBuf, 0, nameLen);
    if (eepromReadBlockCh(ch, CHIP_OFF_NAME, ext, CHIP_NAME_MAX)) {
      memcpy(nameBuf, ext, nameLen < CHIP_NAME_MAX ? nameLen - 1 : CHIP_NAME_MAX);
      nameBuf[nameLen - 1] = '\0';
    }
  }
  return true;
}

bool chipWriteUsesCh(uint8_t ch, uint8_t uses) {
  uint8_t buf[CHIP_HEADER_SIZE];
  if (!eepromReadBlockCh(ch, 0, buf, CHIP_HEADER_SIZE))
    return false;
  buf[CHIP_OFF_USES] = uses;
  uint16_t crc = chip_crc16(buf, CHIP_DATA_SIZE);
  buf[CHIP_OFF_CRC] = (uint8_t)(crc & 0xFF);
  buf[CHIP_OFF_CRC + 1] = (uint8_t)(crc >> 8);
  return eepromWriteBlockCh(ch, 0, buf, CHIP_HEADER_SIZE);
}

bool consumeChipUse(uint8_t ch, ChipHeader &hdr) {
  if (hdr.uses == CHIP_USES_INFINITE)
    return true;
  if (hdr.uses == 0)
    return false;
  hdr.uses--;
  return chipWriteUsesCh(ch, hdr.uses);
}

int firstAppliedEqIndex() {
  if (selectedRow >= 0 && selectedRow < EQ_SLOT_COUNT && eqRt[selectedRow].applied)
    return selectedRow;
  for (uint8_t i = 0; i < EQ_SLOT_COUNT; i++) {
    if (eqRt[i].applied)
      return (int)i;
  }
  return -1;
}

void applyHealAmount(int amount, bool pct) {
  int add = pct ? (playerMaxHP * amount) / 100 : amount;
  playerHP += add;
  clampHP();
  if (playerAgony && playerHP > 0)
    clearAgony();
}

void applyAntiradAmount(int amount, bool pct) {
  int sub = pct ? (playerMaxRad * amount) / 100 : amount;
  playerRad -= sub;
  if (playerRad < 0)
    playerRad = 0;
  antiradUses++;
  if (antiradUses >= 10)
    grantAchievement(ACH_ANTIRAD_10);
  if (hadRadHigh && playerRad == 0)
    grantAchievement(ACH_RAD_CLEAN);
}

bool applyAdminChip(const ChipHeader &hdr, const char *name) {
  switch (hdr.sub) {
  case CHIP_ADM_REVIVE:
    if (playerZombie) {
      setEvent("ЗОМБИ: СДАТЬСЯ", C_RED);
      return false;
    }
    if (!playerDead) {
      setEvent(playerAgony ? "АГОНИЯ: АПТЕЧКА" : "УЖЕ ЖИВ", C_LGRAY);
      return true;
    }
    playerDead = false;
    playerAgony = false;
    playerHP = playerMaxHP;
    playerRad = 0;
    grantAchievement(ACH_REVIVE_FIRST);
    pulseVibro(400);
    setEvent("СВЯЗЬ ВОССТАНОВЛЕНА", C_GREEN);
    return true;
  case CHIP_ADM_MONEY:
    playerMoney += hdr.params[0];
    {
      char buf[40];
      snprintf(buf, sizeof(buf), "АДМИН: %+d RUB", (int)hdr.params[0]);
      setEvent(buf, C_CYAN);
    }
    return true;
  case CHIP_ADM_LEVEL:
    if (hdr.params[0])
      playerLevel = hdr.params[0];
    if (hdr.params[1]) {
      playerXP += hdr.params[1];
      checkLevelUps();
    }
    setEvent("АДМИН: УРОВЕНЬ/XP", C_PURPLE);
    return true;
  case CHIP_ADM_IMMUNITY:
    immuneUntilMs = millis() + (uint32_t)hdr.params[0] * 60000UL;
    setEvent("ИММУНИТЕТ", C_CYAN);
    return true;
  case CHIP_ADM_RESET:
    for (uint8_t i = 0; i < EQ_SLOT_COUNT; i++) {
      eqRt[i].applied = false;
      eqRt[i].rejected = false;
    }
    memset(equipProt, 0, sizeof(equipProt));
    playerHP = cfgStartHP;
    playerMaxHP = cfgMaxHP;
    playerRad = 0;
    playerMoney = cfgStartMoney;
    playerXP = cfgStartXP;
    playerLevel = cfgStartLevel;
    setEvent("СБРОС ПДА", C_ORANGE);
    return true;
  case CHIP_ADM_NEUTRALIZE:
    playerHP = 0;
    playerDead = true;
    playerDeaths++;
    setEvent("НЕЙТРАЛИЗАЦИЯ", C_RED);
    return true;
  case CHIP_ADM_ADMIT:
    if (!admitPending) {
      setEvent("ДОПУСК: УЖЕ ЕСТЬ", C_LGRAY);
      return true;
    }
    admitPending = false;
    completeRegistration(nullptr);
    grantAchievement(ACH_START_FIRST);
    setEvent("ДОПУСК В ИГРУ", C_GREEN);
    return true;
  case CHIP_ADM_REGISTER: {
    char nm[25] = {0};
    if (name && name[0])
      strncpy(nm, name, sizeof(nm) - 1);
    playerEventId = (int)hdr.params[0];
    completeRegistration(nm[0] ? nm : nullptr);
    setEvent("РЕГИСТРАЦИЯ", C_GREEN);
    return true;
  }
  case CHIP_ADM_SAVE: {
    uint8_t snap[48];
    memset(snap, 0, sizeof(snap));
    snap[0] = (uint8_t)(playerHP & 0xFF);
    snap[1] = (uint8_t)((playerHP >> 8) & 0xFF);
    snap[2] = (uint8_t)(playerMaxHP & 0xFF);
    snap[3] = (uint8_t)((playerMaxHP >> 8) & 0xFF);
    snap[4] = (uint8_t)(playerRad & 0xFF);
    snap[5] = (uint8_t)((playerRad >> 8) & 0xFF);
    snap[6] = (uint8_t)(playerLevel & 0xFF);
    snap[7] = (uint8_t)(playerRank & 0xFF);
    snap[8] = (uint8_t)(playerMoney & 0xFF);
    snap[9] = (uint8_t)((playerMoney >> 8) & 0xFF);
    snap[10] = (uint8_t)((playerMoney >> 16) & 0xFF);
    snap[11] = (uint8_t)((playerMoney >> 24) & 0xFF);
    snap[12] = (uint8_t)(playerXP & 0xFF);
    snap[13] = (uint8_t)((playerXP >> 8) & 0xFF);
    snap[14] = (uint8_t)((playerXP >> 16) & 0xFF);
    snap[15] = (uint8_t)((playerXP >> 24) & 0xFF);
    memcpy(snap + 16, achFlags, sizeof(achFlags));
    snap[28] = (uint8_t)cheatShieldCount;
    snap[29] = (uint8_t)(cheatShieldCount >> 8);
    snap[30] = (uint8_t)playerDeaths;
    snap[31] = playerZombie ? 1 : 0;
    eepromWriteBlockCh(UNIVERSAL_SLOT, CHIP_SAVE_ADDR, snap, sizeof(snap));
    setEvent("СНИМОК СОХРАНЁН", C_CYAN);
    return true;
  }
  default:
    setEvent("АДМИН: НЕИЗВЕСТНО", C_ORANGE);
    return false;
  }
}

bool applyConsumableChip(const ChipHeader &hdr) {
  if (playerZombie)
    return false;
  if (playerAgony && hdr.sub != CHIP_SUB_HEAL) {
    setEvent("АГОНИЯ", C_ORANGE);
    return false;
  }
  if (!(cfgFuncFlags & (1 << 7)) && hdr.sub != CHIP_SUB_RESTORE &&
      hdr.sub != CHIP_SUB_UPGRADE) {
    setEvent("РАСХОДНИКИ ВЫКЛ", C_ORANGE);
    return false;
  }
  switch (hdr.sub) {
  case CHIP_SUB_HEAL:
    applyHealAmount(hdr.params[0], hdr.params[1] != 0);
    setEvent("АПТЕЧКА", C_GREEN);
    return true;
  case CHIP_SUB_ANTIRAD:
    applyAntiradAmount(hdr.params[0], hdr.params[1] != 0);
    setEvent("АНТИРАД", C_GREEN);
    return true;
  case CHIP_SUB_REGEN:
    consRegenHp = hdr.params[0];
    consRegenHpUntilMs = millis() + (uint32_t)hdr.params[2] * 1000UL;
    consRegenHpLastMs = millis();
    consRegenRad = hdr.params[3];
    consRegenRadUntilMs = millis() + (uint32_t)hdr.params[5] * 1000UL;
    consRegenRadLastMs = millis();
    setEvent("РЕГЕНЕРАТОР", C_GREEN);
    return true;
  case CHIP_SUB_STIM:
    for (int i = 0; i < 8; i++)
      stimProt[i] = hdr.params[i];
    stimUntilMs = millis() + (uint32_t)hdr.params[8] * 1000UL;
    setEvent("СТИМУЛЯТОР", C_YELLOW);
    return true;
  case CHIP_SUB_RESTORE: {
    int idx = firstAppliedEqIndex();
    if (idx < 0) {
      setEvent("НЕТ ПРЕДМЕТА", C_ORANGE);
      return false;
    }
    uint8_t eqCh = EQ_SLOT_MUX_CH[idx];
    ChipHeader eh;
    if (!readChipHeaderCh(eqCh, eh, nullptr, 0))
      return false;
    if (eh.uses != CHIP_USES_INFINITE) {
      int add = hdr.params[0];
      int next = (int)eh.uses + add;
      if (next > 255)
        next = 255;
      chipWriteUsesCh(eqCh, (uint8_t)next);
    }
    setEvent("РЕМОНТ", C_GREEN);
    return true;
  }
  case CHIP_SUB_UPGRADE: {
    int idx = firstAppliedEqIndex();
    if (idx < 0) {
      setEvent("НЕТ ПРЕДМЕТА", C_ORANGE);
      return false;
    }
    int addHp = hdr.params[0];
    if (addHp > 250)
      addHp = 250;
    if (eqRt[idx].type == CHIP_TYPE_ARMOR) {
      eqRt[idx].params[10] += addHp;
      playerMaxHP += addHp;
      playerHP += addHp;
      clampHP();
    }
    int pct = hdr.params[1];
    if (pct) {
      for (int i = 0; i < 8; i++) {
        int delta = eqRt[idx].params[i] * pct / 100;
        eqRt[idx].params[i] += delta;
        equipProt[i] += delta;
      }
    }
    setEvent("УЛУЧШЕНИЕ", C_GREEN);
    return true;
  }
  default:
    setEvent("РАСХОД: НЕИЗВЕСТНО", C_ORANGE);
    return false;
  }
}

void applyEquipmentBonuses(ChipSlotRt &st, bool add) {
  int sign = add ? 1 : -1;
  if (st.type == CHIP_TYPE_ARMOR) {
    for (int i = 0; i < 8; i++)
      equipProt[i] += sign * st.params[i];
    int bonus = st.params[10];
    playerMaxHP += sign * bonus;
    if (add)
      playerHP += bonus;
    clampHP();
  } else if (st.type == CHIP_TYPE_ARTIFACT) {
    for (int i = 0; i < 7; i++)
      equipProt[i] += sign * st.params[2 + i];
    equipProt[7] += sign * st.params[9];
  }
}

void unapplyEquipment(ChipSlotRt &st) {
  if (!st.applied)
    return;
  applyEquipmentBonuses(st, false);
  st.applied = false;
}

bool tryApplyEquipment(uint8_t ch, ChipSlotRt &st, const ChipHeader &hdr) {
  if (hdr.type == CHIP_TYPE_CONSUMABLE || hdr.type == CHIP_TYPE_ADMIN) {
    setEvent("НЕ ТОТ СЛОТ — CH0", C_ORANGE);
    return false;
  }
  if (admitPending) {
    setEvent("НУЖЕН ДОПУСК", C_RED);
    return false;
  }
  if (playerDead || playerZombie || playerAgony) {
    setEvent(playerZombie ? "ЗОМБИ: СДАТЬСЯ"
                          : (playerAgony ? "АГОНИЯ" : "НУЖНО ВОСКРЕШЕНИЕ"),
             C_RED);
    return false;
  }
  if (hdr.type == CHIP_TYPE_ARMOR) {
    if (!(cfgFuncFlags & (1 << 3))) {
      setEvent("БРОНЯ ВЫКЛ", C_ORANGE);
      return false;
    }
    if (!canUseArmor()) {
      setEvent("БРОНЯ С УР.5", C_ORANGE);
      return false;
    }
    if (countEquippedType(CHIP_TYPE_ARMOR) >= CHIP_MAX_ARMOR) {
      setEvent("БРОНЯ УЖЕ ЕСТЬ", C_ORANGE);
      return false;
    }
  } else if (hdr.type == CHIP_TYPE_ARTIFACT) {
    if (!(cfgFuncFlags & (1 << 4))) {
      setEvent("АРТЫ ВЫКЛ", C_ORANGE);
      return false;
    }
    int have = countEquippedType(CHIP_TYPE_ARTIFACT);
    int need = chip_artifact_level_required(have);
    if (need >= 255) {
      setEvent("ЛИМИТ АРТОВ", C_ORANGE);
      return false;
    }
    if (playerLevel < need) {
      char buf[24];
      snprintf(buf, sizeof(buf), "АРТ С УР.%d", need);
      setEvent(buf, C_ORANGE);
      return false;
    }
  } else {
    setEvent("НЕИЗВЕСТНЫЙ ЧИП", C_ORANGE);
    return false;
  }

  st.type = hdr.type;
  st.sub = hdr.sub;
  memcpy(st.params, hdr.params, sizeof(st.params));
  strncpy(st.label, chipTypeLabel(hdr.type, hdr.sub), sizeof(st.label) - 1);
  st.label[sizeof(st.label) - 1] = '\0';
  applyEquipmentBonuses(st, true);
  st.applied = true;
  st.lastRegenMs = millis();
  if (hdr.type == CHIP_TYPE_ARTIFACT)
    grantAchievement(ACH_ART_FIRST);
  refreshLoadoutAchievements();
  pulseVibro(250);
  char buf[32];
  snprintf(buf, sizeof(buf), "%s CH%u", st.label, (unsigned)ch);
  setEvent(buf, C_GREEN);
  return true;
}

void grantQuestMilestones() {
  if (questsDone >= 1)
    grantAchievement(ACH_QUEST_1);
  if (questsDone >= 5)
    grantAchievement(ACH_QUEST_5);
  if (questsDone >= 15)
    grantAchievement(ACH_QUEST_15);
  if (questsDone >= 30)
    grantAchievement(ACH_QUEST_30);
  if (questsDone >= 50)
    grantAchievement(ACH_QUEST_50);
  if (hiddenQuestsDone >= 1)
    grantAchievement(ACH_HIDDEN_1);
  if (hiddenQuestsDone >= 10)
    grantAchievement(ACH_HIDDEN_10);
  if (playerRank >= 5 && questsDone >= 50)
    grantAchievement(ACH_MONOLITH);
}

void grantSpendMilestones() {
  if (totalSpent >= 1)
    grantAchievement(ACH_BUY_FIRST);
  if (totalSpent >= 1000)
    grantAchievement(ACH_SPEND_1K);
  if (totalSpent >= 5000)
    grantAchievement(ACH_SPEND_5K);
  if (totalSpent >= 20000)
    grantAchievement(ACH_SPEND_20K);
  if (totalSpent >= 50000)
    grantAchievement(ACH_SPEND_50K);
  if (totalSpent >= 100000)
    grantAchievement(ACH_SPEND_100K);
}

void grantEarnMilestones() {
  if (totalEarned >= 1)
    grantAchievement(ACH_SELL_FIRST);
  if (totalEarned >= 2000)
    grantAchievement(ACH_EARN_2K);
  if (totalEarned >= 10000)
    grantAchievement(ACH_EARN_10K);
  if (totalEarned >= 50000)
    grantAchievement(ACH_EARN_50K);
  if (totalEarned >= 200000)
    grantAchievement(ACH_EARN_200K);
}

void refreshLoadoutAchievements() {
  int armor = countEquippedType(CHIP_TYPE_ARMOR);
  int arts = countEquippedType(CHIP_TYPE_ARTIFACT);
  int filled = 0;
  for (uint8_t i = 0; i < EQ_SLOT_COUNT; i++) {
    if (eqRt[i].applied)
      filled++;
  }
  if (armor)
    grantAchievement(ACH_ARMOR_FIRST);
  if (armor && arts >= 3)
    grantAchievement(ACH_FULL_LOADOUT);
  if (filled >= EQ_SLOT_COUNT)
    grantAchievement(ACH_SLOTS_ALL);
  if (arts >= 3)
    grantAchievement(ACH_ART_TYPES_3);
}

bool applyQuestChip(const ChipHeader &hdr, const char *name) {
  bool complete = hdr.params[0] != 0;
  bool hidden = hdr.params[1] != 0;
  int reward = hdr.params[2];
  if (hidden && !canUseHiddenQuest()) {
    setEvent("СКРЫТЫЙ ЛВ20", C_RED);
    return false;
  }
  char qid[12];
  snprintf(qid, sizeof(qid), "q%u", (unsigned)hdr.sub);
  if (!complete) {
    if (findActiveQuest(qid) >= 0) {
      setEvent("КВЕСТ: УЖЕ АКТИВЕН", C_ORANGE);
      return false;
    }
    if (activeTaskCount >= ACTIVE_TASKS_MAX) {
      setEvent("КВЕСТ: ЛИМИТ", C_RED);
      return false;
    }
    ActiveTaskStub &t = activeTasks[activeTaskCount++];
    strncpy(t.id, qid, sizeof(t.id) - 1);
    t.id[sizeof(t.id) - 1] = '\0';
    if (name && name[0])
      strncpy(t.title, name, sizeof(t.title) - 1);
    else
      snprintf(t.title, sizeof(t.title), "Задание #%u", (unsigned)hdr.sub);
    t.title[sizeof(t.title) - 1] = '\0';
    strncpy(t.shortDesc, hidden ? "Скрытый квест" : "Квест-чип CH0",
            sizeof(t.shortDesc) - 1);
    pulseVibro(250);
    setEvent("КВЕСТ ПРИНЯТ", C_YELLOW);
    return true;
  }
  int idx = findActiveQuest(qid);
  if (idx >= 0) {
    for (uint8_t i = (uint8_t)idx; i + 1 < activeTaskCount; i++)
      activeTasks[i] = activeTasks[i + 1];
    if (activeTaskCount > 0)
      activeTaskCount--;
  }
  questsDone++;
  if (hidden)
    hiddenQuestsDone++;
  if (reward > 0) {
    playerMoney += reward;
    playerXP += reward;
    checkLevelUps();
  }
  grantQuestMilestones();
  pulseVibro(250);
  setEvent("КВЕСТ СДАН", C_GREEN);
  return true;
}

bool applyShopChip(const ChipHeader &hdr) {
  if (!canUseStore()) {
    setEvent("МАГАЗИН: МАЛО УРОВНЯ", C_ORANGE);
    return false;
  }
  int price = hdr.params[0];
  if (price > 0) {
    int paid = calcShopPrice(price);
    if (playerMoney < paid) {
      setEvent("НЕ ХВАТАЕТ", C_RED);
      return false;
    }
    playerMoney -= paid;
    totalSpent += paid;
    grantSpendMilestones();
    char buf[32];
    snprintf(buf, sizeof(buf), "МАГАЗИН: -%d RUB", paid);
    setEvent(buf, C_CYAN);
    return true;
  }
  if (price < 0) {
    int got = -price;
    playerMoney += got;
    totalEarned += got;
    grantEarnMilestones();
    char buf[32];
    snprintf(buf, sizeof(buf), "ПРОДАЖА: +%d RUB", got);
    setEvent(buf, C_CYAN);
    return true;
  }
  setEvent("МАГАЗИН", C_CYAN);
  return true;
}

bool tryApplyCh0(const ChipHeader &hdr, const char *name) {
  if (hdr.type == CHIP_TYPE_ARMOR || hdr.type == CHIP_TYPE_ARTIFACT) {
    setEvent("ВСТАВЬ В СЛОТ 2/3/5/6", C_ORANGE);
    return false;
  }
  if (hdr.type == CHIP_TYPE_ADMIN)
    return applyAdminChip(hdr, name);
  if (admitPending) {
    setEvent("НУЖЕН ДОПУСК", C_RED);
    return false;
  }
  if (playerZombie)
    return false;
  if (playerDead) {
    setEvent("НУЖНО ВОСКРЕШЕНИЕ", C_RED);
    return false;
  }
  if (playerAgony && hdr.type != CHIP_TYPE_CONSUMABLE) {
    setEvent("АГОНИЯ", C_ORANGE);
    return false;
  }
  if (hdr.type == CHIP_TYPE_QUEST)
    return applyQuestChip(hdr, name);
  if (hdr.type == CHIP_TYPE_SHOP)
    return applyShopChip(hdr);
  if (hdr.type == CHIP_TYPE_CONSUMABLE)
    return applyConsumableChip(hdr);
  setEvent("НЕИЗВЕСТНЫЙ ЧИП", C_ORANGE);
  return false;
}

void tickSlotRegen(ChipSlotRt &st) {
  if (!st.applied || playerDead || playerZombie || playerAgony || admitPending)
    return;
  int amount = 0;
  int intervalSec = 0;
  if (st.type == CHIP_TYPE_ARMOR) {
    amount = st.params[8];
    intervalSec = st.params[9];
  } else if (st.type == CHIP_TYPE_ARTIFACT) {
    amount = st.params[0];
    intervalSec = st.params[1];
  }
  if (intervalSec <= 0 || amount == 0)
    return;
  uint32_t iv = (uint32_t)intervalSec * 1000UL;
  if (millis() - st.lastRegenMs < iv)
    return;
  st.lastRegenMs = millis();
  playerHP += amount;
  clampHP();
}

void tickConsumableRegen() {
  if (playerDead || playerZombie || playerAgony || admitPending)
    return;
  uint32_t now = millis();
  if (consRegenHp && now < consRegenHpUntilMs && now - consRegenHpLastMs >= 1000) {
    consRegenHpLastMs = now;
    playerHP += consRegenHp;
    clampHP();
  }
  if (consRegenRad && now < consRegenRadUntilMs && now - consRegenRadLastMs >= 1000) {
    consRegenRadLastMs = now;
    playerRad -= consRegenRad;
    if (playerRad < 0)
      playerRad = 0;
  }
}

void pollOneChipSlot(uint8_t ch, ChipSlotRt &st, bool isCh0) {
  bool present = chipPresentOnCh(ch);
  if (!present) {
    if (isCh0 && chipConfirmPending && pendingChipCh == ch) {
      chipConfirmPending = false;
      pendingChipName[0] = '\0';
    }
    if (st.applied)
      unapplyEquipment(st);
    memset(&st, 0, sizeof(st));
    return;
  }
  if (!st.present) {
    st.present = true;
    st.detectMs = millis();
    st.settled = false;
    return;
  }
  if (!st.settled) {
    if (millis() - st.detectMs < CHIP_HOTPLUG_MS)
      return;
    st.settled = true;
  }
  if (st.applied) {
    tickSlotRegen(st);
    return;
  }
  if (st.rejected)
    return;
  if (isCh0 && chipConfirmPending)
    return;
  if (isCh0 && ch0TxnBusy())
    return;

  ChipHeader hdr;
  char name[25] = {0};
  if (!readChipHeaderCh(ch, hdr, name, sizeof(name))) {
    st.rejected = true;
    return;
  }
  if (hdr.uses == 0) {
    strncpy(st.label, chipTypeLabel(hdr.type, hdr.sub), sizeof(st.label) - 1);
    st.rejected = true;
    return;
  }

  if (isCh0 && hdr.type == CHIP_TYPE_CONSUMABLE) {
    if (playerZombie || playerDead) {
      strncpy(st.label, chipTypeLabel(hdr.type, hdr.sub), sizeof(st.label) - 1);
      st.rejected = true;
      return;
    }
    if (playerAgony && hdr.sub != CHIP_SUB_HEAL) {
      strncpy(st.label, chipTypeLabel(hdr.type, hdr.sub), sizeof(st.label) - 1);
      st.rejected = true;
      setEvent("АГОНИЯ", C_ORANGE);
      return;
    }
    chipConfirmPending = true;
    pendingChip = hdr;
    pendingChipCh = ch;
    strncpy(pendingChipName, name[0] ? name : chipTypeLabel(hdr.type, hdr.sub),
            sizeof(pendingChipName) - 1);
    pendingChipName[sizeof(pendingChipName) - 1] = '\0';
    strncpy(st.label, chipTypeLabel(hdr.type, hdr.sub), sizeof(st.label) - 1);
    st.type = hdr.type;
    st.sub = hdr.sub;
    pulseVibro(200);
    setEvent("ИСПОЛЬЗОВАТЬ? ОК/ESC", C_YELLOW);
    needFullRedraw = true;
    return;
  }

  bool ok = false;
  if (isCh0)
    ok = tryApplyCh0(hdr, name);
  else
    ok = tryApplyEquipment(ch, st, hdr);

  if (!ok) {
    strncpy(st.label, chipTypeLabel(hdr.type, hdr.sub), sizeof(st.label) - 1);
    st.type = hdr.type;
    st.sub = hdr.sub;
    st.rejected = true;
    needFullRedraw = true;
    return;
  }
  if (isCh0) {
    consumeChipUse(ch, hdr);
    strncpy(st.label, chipTypeLabel(hdr.type, hdr.sub), sizeof(st.label) - 1);
    st.type = hdr.type;
    st.sub = hdr.sub;
    st.applied = true;
  }
  saveState();
  needFullRedraw = true;
}

void pollChips() {
  pollOneChipSlot(UNIVERSAL_SLOT, ch0Rt, true);
  for (uint8_t i = 0; i < EQ_SLOT_COUNT; i++)
    pollOneChipSlot(EQ_SLOT_MUX_CH[i], eqRt[i], false);
  tickConsumableRegen();
}

bool isCombatLocked() {
  return admitPending || playerDead || playerZombie;
}

bool inAgony() { return playerAgony && !playerDead && !playerZombie; }

void markAgony() {
  playerHP = 0;
  playerAgony = true;
  playerDead = false;
  playerZombie = false;
  pulseVibro(400);
  setEvent("АГОНИЯ", C_ORANGE);
  saveState();
}

void clearAgony() {
  if (!playerAgony)
    return;
  playerAgony = false;
  setEvent("АГОНИЯ ПРОШЛА", C_GREEN);
  saveState();
}

void markDead() {
  playerHP = 0;
  playerDead = true;
  playerAgony = false;
  playerZombie = false;
  playerDeaths++;
  grantAchievement(ACH_DEATH_FIRST);
  if (playerDeaths >= 5)
    grantAchievement(ACH_DEATHS_5);
  pulseVibro(500);
  setEvent("СВЯЗЬ ПОТЕРЯНА", C_RED);
  saveState();
}

void markZombie() {
  playerHP = 0;
  playerDead = false;
  playerAgony = false;
  playerZombie = true;
  playerRad = playerMaxRad;
  pulseVibro(500);
  setEvent("ВЫ ЗОМБИ", C_PURPLE, true);
  saveState();
}

void doSurrender() {
  if (admitPending)
    return;
  if (playerZombie)
    playerZombie = false;
  playerAgony = false;
  if (!playerDead)
    markDead();
}

void startEmission(int timerSec, int durSec) {
  if (timerSec < 0)
    timerSec = 0;
  if (durSec < 0)
    durSec = 0;
  emissionTimer = timerSec;
  emissionDuration = durSec;
  emissionStrikeTick = 0;
  pulseVibro(400);
  setEvent("ВЫБРОС!", C_RED, true);
  if (isAudioReady)
    myDFPlayer.playMp3Folder(2);
}

void accumulateInnate(int dmgType, int actual) {
  if (dmgType < 0 || dmgType > 6 || actual <= 0)
    return;
  dmgAcc[dmgType] += actual;
  while (dmgAcc[dmgType] >= RESISTANCE_THRESHOLD) {
    dmgAcc[dmgType] -= RESISTANCE_THRESHOLD;
    if (innateRes[dmgType] < RESISTANCE_CAP) {
      innateRes[dmgType]++;
      grantAchievement(ACH_RES_PLUS_1);
      if (innateRes[dmgType] >= 10)
        grantAchievement(ACH_RES_10);
      if (innateRes[dmgType] >= RESISTANCE_CAP)
        grantAchievement(ACH_RES_50_CAP);
    }
  }
}

void applyRadGain(int actualRad) {
  if (actualRad <= 0)
    return;
  playerRad += actualRad;
  if (playerRad > playerMaxRad)
    playerRad = playerMaxRad;
  if (playerRad * 2 >= playerMaxRad) {
    hadRadHigh = true;
    grantAchievement(ACH_RAD_50);
  }
  pulseVibro(200);
  if (playerRad >= playerMaxRad) {
    markZombie();
    return;
  }
  saveState();
}

int applyHpDamage(int dmg, uint16_t mask, const char *source, bool iframe) {
  if (admitPending || playerDead || playerZombie || dmg <= 0)
    return 0;
  if (playerAgony) {
    if (source && (!strcmp(source, "EMISSION") || !strcmp(source, "PSI")))
      markZombie();
    else
      markDead();
    return dmg;
  }
  if (iframe && lastIframeMs && (millis() - lastIframeMs < IFRAME_MS) &&
      memcmp(lastIframeMac, anomalyMac, 6) == 0)
    return 0;
  int res = 0;
  int usedType = -1;
  for (int b = 0; b < 7; b++) {
    if (mask & (1 << b)) {
      int r = protForType(b);
      if (isInSafeZone())
        r += zoneProt[b];
      r = min(100, r);
      if (r > res)
        res = r;
      usedType = b;
    }
  }
  int actual = dmg * (100 - res) / 100;
  if (actual < 0)
    actual = 0;
  if (immuneUntilMs && millis() < immuneUntilMs)
    actual = 0;
  if (usedType >= 0)
    accumulateInnate(usedType, actual);
  if (actual > 0) {
    totalHpDamage += actual;
    if (totalHpDamage >= 100)
      grantAchievement(ACH_DMG_100);
    if (totalHpDamage >= 500)
      grantAchievement(ACH_DMG_500);
    if (totalHpDamage >= 2000)
      grantAchievement(ACH_DMG_2000);
  }
  playerHP -= actual;
  lastDamageMs = millis();
  lastIframeMs = millis();
  memcpy(lastIframeMac, anomalyMac, 6);
  pulseVibro(250);
  if (playerHP <= 0) {
    playerHP = 0;
    if (source && (!strcmp(source, "EMISSION") || !strcmp(source, "PSI")))
      markZombie();
    else
      markAgony();
  } else {
    saveState();
  }
  return actual;
}

void confirmPendingChip() {
  if (!chipConfirmPending)
    return;
  bool ok = applyConsumableChip(pendingChip);
  if (ok) {
    consumeChipUse(pendingChipCh, pendingChip);
    ch0Rt.applied = true;
    strncpy(ch0Rt.label, chipTypeLabel(pendingChip.type, pendingChip.sub),
            sizeof(ch0Rt.label) - 1);
    saveState();
  } else {
    ch0Rt.rejected = true;
  }
  chipConfirmPending = false;
  needFullRedraw = true;
}

void rejectPendingChip() {
  if (!chipConfirmPending)
    return;
  chipConfirmPending = false;
  ch0Rt.rejected = true;
  strncpy(ch0Rt.label, chipTypeLabel(pendingChip.type, pendingChip.sub),
          sizeof(ch0Rt.label) - 1);
  setEvent("ОТМЕНА", C_LGRAY);
  needFullRedraw = true;
}

void gameTick() {
  if (admitPending)
    return;

  if (inAnomalyZone && lastAnomalyPacketMs &&
      (millis() - lastAnomalyPacketMs > ANTIPHASE_TIMEOUT_MS)) {
    inAnomalyZone = false;
    if (lastAnomalyRssi > RSSI_SAFE_EXIT) {
      cheatShieldCount++;
      if (cheatShieldCount >= 10)
        grantAchievement(ACH_ANTICHEAT_10);
      saveState();
      setEvent("СИГНАЛ ПРЕРВАН", C_ORANGE);
    } else if (!playerDead && !playerZombie && playerHP > playerMaxHP / 10) {
      grantAchievement(ACH_EXIT_ALIVE);
    }
  }

  static bool detLatched = false;
  bool detNow = isAnomalyDetectorActive();
  if (detNow && !detLatched) {
    detectorTriggers++;
    grantAchievement(ACH_DETECTOR_FIRST);
    if (detectorTriggers >= 50)
      grantAchievement(ACH_DETECTOR_50);
  }
  detLatched = detNow;

  if (emissionTimer > 0) {
    emissionTimer--;
    if (emissionTimer == 3600)
      setEvent("ВЫБРОС ЧЕРЕЗ 1Ч", C_RED, true);
    else if (emissionTimer == 1800)
      setEvent("ВЫБРОС ЧЕРЕЗ 30М", C_RED, true);
    else if (emissionTimer == 900)
      setEvent("ВЫБРОС ЧЕРЕЗ 15М", C_RED, true);
    else if (emissionTimer == 300)
      setEvent("ВЫБРОС ЧЕРЕЗ 5М", C_RED, true);
    if (emissionTimer == 3600 || emissionTimer == 1800 ||
        emissionTimer == 900 || emissionTimer == 300)
      pulseVibro(400);
  } else if (emissionTimer == 0) {
    if (emissionDuration > 0) {
      emissionDuration--;
      emissionStrikeTick++;
      bool sheltered = isInSafeZone() && szEmissionProtect;
      if (!sheltered && !isCombatLocked() && (emissionStrikeTick % 10 == 0)) {
        applyHpDamage(EMISSION_DMG_TICK, (1 << 5), "EMISSION", false);
        if (!playerZombie)
          applyRadGain(EMISSION_RAD_TICK);
      }
    } else {
      emissionTimer = -1;
      emissionStrikeTick = 0;
      setEvent("ВЫБРОС ОКОНЧЕН", C_GREEN, true);
      pulseVibro(300);
    }
  }

  if (isCombatLocked() || playerAgony)
    return;

  if (playerRad * 2 > playerMaxRad) {
    radTickCounter++;
    grantAchievement(ACH_RAD_SICK);
    if (radTickCounter >= 60) {
      radTickCounter = 0;
      applyHpDamage(RAD_SICKNESS_HP, 0, "RAD", false);
    }
  } else {
    radTickCounter = 0;
  }

  if (discoveredMacCount >= 20 && playerLevel >= 20)
    grantAchievement(ACH_STALKER_KRAFT);
}

// =====================================================
// NVS — ЗАГРУЗКА / СОХРАНЕНИЕ
// =====================================================
Preferences prefs;

void loadState() {
  prefs.begin("pda", true);
  playerHP = prefs.getInt("hp", cfgStartHP);
  playerMaxHP = cfgMaxHP; // бонус брони начисляется с чипов при pollChips
  playerRad = prefs.getInt("rad", 0);
  playerMaxRad = prefs.getInt("max_rad", cfgMaxRad);
  playerMoney = prefs.getInt("money", cfgStartMoney);
  playerXP = prefs.getInt("xp", cfgStartXP);
  playerLevel = prefs.getInt("lvl", cfgStartLevel);
  playerDeaths = prefs.getInt("deaths", 0);
  playerDead = prefs.getBool("dead", false);
  playerZombie = prefs.getBool("zombie", false);
  playerAgony = prefs.getBool("agony", false);
  if (playerDead || playerZombie)
    playerAgony = false;
  else if (playerHP <= 0)
    playerAgony = true;
  playerRank = prefs.getInt("rank", 0);
  achFlags[0] = prefs.getUInt("ach", 0);
  achFlags[1] = prefs.getUInt("ach1", 0);
  achFlags[2] = prefs.getUInt("ach2", 0);
  cheatShieldCount = prefs.getInt("shield", 0);
  questsDone = prefs.getInt("qdone", 0);
  hiddenQuestsDone = prefs.getInt("qhid", 0);
  totalHpDamage = prefs.getInt("tdmg", 0);
  totalSpent = prefs.getInt("spent", 0);
  totalEarned = prefs.getInt("earned", 0);
  antiradUses = prefs.getInt("arad", 0);
  actorRole = (int8_t)prefs.getChar("role", ROLE_STALKER);
  zzHealTotal = prefs.getInt("zzhp", 0);
  detectorTriggers = prefs.getInt("detn", 0);
  arenaFights = prefs.getInt("afight", 0);
  arenaWins = prefs.getInt("awin", 0);
  bankVolume = prefs.getInt("bankv", 0);
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
  activeTaskCount = prefs.getUChar("atn", 0);
  if (activeTaskCount > ACTIVE_TASKS_MAX)
    activeTaskCount = ACTIVE_TASKS_MAX;
  memset(activeTasks, 0, sizeof(activeTasks));
  size_t atGot = prefs.getBytes("atb", activeTasks, sizeof(activeTasks));
  if (atGot != sizeof(activeTasks))
    activeTaskCount = 0;
  for (uint8_t i = 0; i < activeTaskCount; i++) {
    activeTasks[i].id[sizeof(activeTasks[i].id) - 1] = '\0';
    activeTasks[i].title[sizeof(activeTasks[i].title) - 1] = '\0';
    activeTasks[i].shortDesc[sizeof(activeTasks[i].shortDesc) - 1] = '\0';
  }
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
  prefs.putBool("zombie", playerZombie);
  prefs.putBool("agony", playerAgony);
  prefs.putInt("rank", playerRank);
  prefs.putUInt("ach", achFlags[0]);
  prefs.putUInt("ach1", achFlags[1]);
  prefs.putUInt("ach2", achFlags[2]);
  prefs.putInt("shield", cheatShieldCount);
  prefs.putInt("qdone", questsDone);
  prefs.putInt("qhid", hiddenQuestsDone);
  prefs.putInt("tdmg", totalHpDamage);
  prefs.putInt("spent", totalSpent);
  prefs.putInt("earned", totalEarned);
  prefs.putInt("arad", antiradUses);
  prefs.putChar("role", (char)actorRole);
  prefs.putInt("zzhp", zzHealTotal);
  prefs.putInt("detn", detectorTriggers);
  prefs.putInt("afight", arenaFights);
  prefs.putInt("awin", arenaWins);
  prefs.putInt("bankv", bankVolume);
  prefs.putBool("reg", playerRegistered);
  prefs.putInt("eid", playerEventId);
  prefs.putString("auid", playerAssignedUid);
  prefs.putString("pname", playerName);
  prefs.putString("pcsign", playerCallsign);
  prefs.putString("pgroup", playerGroup);
  prefs.putUChar("mac_n", discoveredMacCount);
  prefs.putBytes("mac_b", discoveredMacs, discoveredMacCount * 6);
  prefs.putUChar("atn", activeTaskCount);
  prefs.putBytes("atb", activeTasks, sizeof(activeTasks));
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
    Serial.println("STALKER:PDA:v2.8");
    return;
  }

  if (line.startsWith("LORA_TX:")) {
    if (!loraOk) {
      Serial.println("ERROR:NO_LORA");
      return;
    }
    uint8_t src = (uint8_t)constrain(playerEventId, 0, 255);
    if (stalkerHandleLoraTxLine(line, src))
      Serial.println("OK");
    else
      Serial.println("ERROR:BAD_LORA");
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
    grantAchievement(ACH_START_FIRST);
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
    playerAgony = false;
    playerHP = playerMaxHP;
    playerRad = 0;
    grantAchievement(ACH_REVIVE_FIRST);
    pulseVibro(400);
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
      setEvent(text.c_str(), C_YELLOW, true);
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
      dur = 60;
    startEmission(timer, dur);
    Serial.println("OK");
    return;
  }

  if (line.startsWith("CONFIG:ROLE:")) {
    String data = line.substring(strlen("CONFIG:ROLE:"));
    int role = parseIntValue(data, "role");
    if (role >= 0 && role <= ROLE_MUTANT)
      actorRole = (int8_t)role;
    saveState();
    Serial.println("OK");
    return;
  }

  if (line.startsWith("CONFIG:KILL")) {
    if (!admitPending)
      markDead();
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
    Serial.print("ZOMBIE:");
    Serial.println(playerZombie ? 1 : 0);
    Serial.print("AGONY:");
    Serial.println(playerAgony ? 1 : 0);
    Serial.print("SHIELD:");
    Serial.println(cheatShieldCount);
    Serial.print("ROLE:");
    Serial.println((int)actorRole);
    Serial.print("EMISSION:");
    Serial.print(emissionTimer);
    Serial.print(",");
    Serial.println(emissionDuration);
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

volatile bool newHealReceived = false;
volatile int16_t incomingHealAmount = 0;

volatile bool newRadReceived = false;
volatile int16_t incomingRadAmount = 0;

volatile bool newSafeZoneReceived = false;
volatile int16_t szHealAmount = 0;
volatile int16_t szRadAmount = 0;

uint32_t lastSafeZoneMs = 0;
int zoneProt[8] = {0};

bool isInSafeZone() {
  bool recent = lastSafeZoneMs > 0 && (millis() - lastSafeZoneMs < SZ_TIMEOUT_MS);
  if (!recent)
    return false;
  if (!bu03Present)
    return true;
  return uwbInShelter;
}

void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(Packet))
    return;

  Packet pkt;
  memcpy(&pkt, data, sizeof(Packet));
  applyIncomingPacket(pkt, info ? info->src_addr : nullptr);
  if (pkt.emitter == EMITTER_ANOMALY &&
      (pkt.msg_type == MSG_DAMAGE || pkt.msg_type == MSG_RADIATION))
    noteAnomalySignal(info);
}

void applyIncomingPacket(const Packet &pkt, const uint8_t *mac) {
  if (pkt.msg_type == MSG_ZONE_ASSIGN) {
    pendingZoneSlot = (int)pkt.val1;
    if (mac)
      memcpy(pendingZoneMac, mac, 6);
    pendingZoneAssign = true;
    return;
  }

  if (pkt.msg_type == MSG_DAMAGE && pkt.val1 > 0) {
    incomingFromPlayer = (pkt.emitter == EMITTER_PLAYER);
    incomingControllerPsi = incomingFromPlayer && ((pkt.val3 & (1 << 5)) != 0);
    if (!incomingFromPlayer && !(cfgFuncFlags & (1 << 5)))
      return;
    if (pkt.emitter == EMITTER_ANOMALY)
      noteAnomalySignal(nullptr);
    incomingDmgAmount = pkt.val1;
    incomingDmgMask = pkt.val3;
    if (mac)
      memcpy(anomalyMac, mac, 6);
    if (pkt.emitter == EMITTER_ANOMALY)
      lastZoneAssignMs = millis();
    newDamageReceived = true;

  } else if (pkt.msg_type == MSG_HEAL && pkt.val1 > 0) {
    incomingHealAmount = pkt.val1;
    newHealReceived = true;

  } else if (pkt.msg_type == MSG_RADIATION && pkt.val1 > 0) {
    if (!(cfgFuncFlags & (1 << 1)))
      return; // RAD отключена
    if (pkt.emitter == EMITTER_ANOMALY)
      noteAnomalySignal(nullptr);
    incomingRadAmount = pkt.val1;
    newRadReceived = true;

  } else if (pkt.msg_type == MSG_SAFE_ZONE) {
    lastSafeZoneMs = millis();
    if (pkt.val3 == SZ_BEACON_FLAG) {
      szEmissionProtect = pkt.val1 != 0;
      if (pkt.val2 > 0)
        szRadiusM = pkt.val2;
      if (mac)
        memcpy(shelterMac, mac, 6);
      wantShelterUwb = true;
    } else if (pkt.val3 == SZ_PROT_FLAG) {
      if (!bu03Present || uwbInShelter) {
        int t = (int)pkt.val1;
        if (t >= 0 && t <= 7)
          zoneProt[t] = (int)pkt.val2;
      }
    } else if (!bu03Present || uwbInShelter) {
      szHealAmount = pkt.val1;
      szRadAmount = pkt.val2;
      newSafeZoneReceived = true;
    }

  } else if (pkt.msg_type == MSG_EMISSION) {
    incomingEmissionTimer = pkt.val1;
    incomingEmissionDur = pkt.val2;
    newEmissionReceived = true;

  } else if (pkt.msg_type == MSG_COMMAND) {
    incomingCmd = pkt.val1;
    incomingCmdVal2 = pkt.val2;
    newCommandReceived = true;

  } else if (pkt.msg_type == MSG_RADIO) {
    incomingRadioTrack = pkt.val1;
    incomingRadioVol = pkt.val2;
    newRadioReceived = true;
  }
}

static uint8_t kBcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void sendEspNowPkt(const uint8_t *mac, uint8_t emitter, uint8_t msg,
                   int16_t v1, int16_t v2, int16_t v3) {
  if (!mac)
    return;
  Packet pkt;
  pkt.emitter = emitter;
  pkt.msg_type = msg;
  pkt.val1 = v1;
  pkt.val2 = v2;
  pkt.val3 = v3;
  if (!esp_now_is_peer_exist(mac)) {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 1;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
  }
  esp_now_send(mac, (uint8_t *)&pkt, sizeof(pkt));
}

// =====================================================
// ДИСПЛЕЙ + U8G2
// =====================================================
Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
U8G2_FOR_ADAFRUIT_GFX u8g2;

int8_t currentPage = 0;
#define NUM_PAGES 3
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
  bu03Present = true;
  bu03StoreRaw(bu03Rx);
  bu03SendCmd("AT+SETUWBMODE=0", false);
  Serial.println("[BU03] AT OK");
  return true;
}

bool bu03SetCfg(int id, int role) {
  char cmd[48];
  snprintf(cmd, sizeof(cmd), "AT+SETCFG=%d,%d,%d,%d", id, role, BU03_CH_POLY,
           BU03_RATE_POLY);
  if (!bu03SendCmd(cmd, false) || bu03Rx.indexOf("ERR") >= 0) {
    Serial.printf("[BU03] SETCFG id=%d role=%d FAIL\n", id, role);
    return false;
  }
  bu03SendCmd("AT+SAVE", false);
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
  for (int i = 0; i < NUM_PAGES; i++) {
    int x = SCR_W - 74 + i * 12;
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

void setEvent(const char *text, uint16_t color, bool admin) {
  if (playerZombie && !admin)
    return;
  strncpy(eventText, text, 47);
  eventText[47] = '\0';
  eventColor = color;
  eventTimeMs = millis();
  needFullRedraw = true;
  if (notifyCount < 5) {
    strncpy(notifyLog[notifyCount], eventText, 47);
    notifyLog[notifyCount][47] = '\0';
    notifyCount++;
  } else {
    for (uint8_t i = 0; i < 4; i++)
      memcpy(notifyLog[i], notifyLog[i + 1], 48);
    strncpy(notifyLog[4], eventText, 47);
    notifyLog[4][47] = '\0';
  }
  notifyIdx = notifyCount ? notifyCount - 1 : 0;
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
    y += 22;
  }

  if (inAgony())
    printRus(12, y, "АГОНИЯ", C_RED);
  else if (actorRole == ROLE_CONTROLLER)
    printRus(12, y, "КОНТРОЛЛЕР", C_PURPLE);
  else if (actorRole == ROLE_MUTANT)
    printRus(12, y, "МУТАНТ", C_ORANGE);

  if (emissionTimer > 0) {
    char em[28];
    snprintf(em, sizeof(em), "ВЫБРОС через %dс", emissionTimer);
    printRusStr(12, EVT_Y - 24, String(em), C_RED);
  } else if (emissionTimer == 0 && emissionDuration > 0) {
    printRus(12, EVT_Y - 24, "ВЫБРОС!", C_RED);
  } else if (notifyCount) {
    uint8_t i2 = notifyIdx > 0 ? notifyIdx - 1 : notifyIdx;
    printRusStr(12, EVT_Y - 24, String(notifyLog[i2]), C_LGRAY);
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

  const char *slotNames[] = {"CH2", "CH3", "CH5", "CH6"};
  char ch0line[40];
  if (ch0Rt.present && ch0Rt.label[0])
    snprintf(ch0line, sizeof(ch0line), "CH0: %s", ch0Rt.label);
  else
    snprintf(ch0line, sizeof(ch0line), "CH0: [---]");
  printRusStr(12, 62, String(ch0line), ch0Rt.applied ? C_GREEN : C_BORDER);

  for (int i = 0; i < 4; i++) {
    int y = 84 + i * ROW_H;
    char row[40];
    if (eqRt[i].applied && eqRt[i].label[0])
      snprintf(row, sizeof(row), "%s: %s", slotNames[i], eqRt[i].label);
    else if (eqRt[i].present && eqRt[i].label[0])
      snprintf(row, sizeof(row), "%s: %s?", slotNames[i], eqRt[i].label);
    else
      snprintf(row, sizeof(row), "%s: [---]", slotNames[i]);
    if (selectedRow == i) {
      tft.fillRect(8, y - 2, SCR_W - 16, ROW_H - 2, C_DGRAY);
      printRus(16, y, ">", C_WHITE);
      printRusStr(32, y, String(row), C_WHITE);
    } else {
      tft.fillRect(8, y - 2, SCR_W - 16, ROW_H - 2, C_BLACK);
      printRusStr(24, y, String(row), eqRt[i].applied ? C_GREEN : C_BORDER);
    }
  }

  printRus(12, 188, "ВНИЗ — слот ремонта/улучш.", C_DGRAY);
  drawPageIndicator();
}
void drawPage2() {
  printRus(84, 8, "СОПРОТИВЛЕНИЯ", C_BORDER);
  tft.drawFastHLine(8, 28, SCR_W - 16, C_BORDER);

  bool inZone = isInSafeZone();

  for (int i = 0; i < 7; i++) {
    int y = 40 + i * 22;
    tft.fillRect(8, y - 2, SCR_W - 16, 20, C_BLACK);
    printRus(12, y, DMG_NAMES[i], C_WHITE);

    String valTxt = String(protForType(i)) + "%";
    uint16_t col = protForType(i) > 0 ? C_GREEN : C_BORDER;
    printRusStr(180, y, valTxt, col);

    if (inZone && zoneProt[i] > 0) {
      String zoneTxt = "(+" + String(zoneProt[i]) + "%)";
      printRusStr(230, y, zoneTxt, C_GREEN);
    }
  }

  int y = 40 + 7 * 22;
  tft.fillRect(8, y - 2, SCR_W - 16, 20, C_BLACK);
  printRus(12, y, "RAD", C_ORANGE);
  String radTxt = String(protForType(7)) + "%";
  printRusStr(180, y, radTxt, protForType(7) > 0 ? C_ORANGE : C_BORDER);
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
  snprintf(pageLbl, sizeof(pageLbl), "стр. %d/%d", currentPage + 1, NUM_PAGES);
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

// ─── СТРАНИЦА 4: ЗАДАНИЯ (активные + EEPROM CH0 квесты) ───
void drawPage4() {
  printRus(108, 8, "ЗАДАНИЯ", C_BORDER);
  tft.drawFastHLine(8, 28, SCR_W - 16, C_BORDER);
  if (activeTaskCount == 0) {
    printRus(72, 80, "НЕТ АКТИВНЫХ", C_DGRAY);
    printRus(24, 110, "Подключите доску", C_DGRAY);
  } else {
    for (uint8_t i = 0; i < activeTaskCount && i < ACTIVE_TASKS_MAX; i++) {
      int y = 44 + i * 28;
      uint16_t col = (i == questSel) ? C_WHITE : C_LGRAY;
      if (i == questSel)
        tft.fillRect(8, y - 2, SCR_W - 16, 24, C_DGRAY);
      printRus(12, y, i == questSel ? ">" : " ", col);
      printRusStr(28, y, String(activeTasks[i].title), col);
    }
    if (boardQuestCount)
      printRus(16, 188, "ОК = сдать на доске", C_YELLOW);
    else
      printRus(16, 188, "ОК = описание", C_DGRAY);
  }
  drawPageIndicator();
}

void drawBoardOverlay() {
  tft.fillRect(12, 28, SCR_W - 24, 176, C_BLACK);
  tft.drawRect(12, 28, SCR_W - 24, 176, C_YELLOW);
  printRus(72, 36, "ДОСКА ЗАДАНИЙ", C_YELLOW);
  if (boardVisibleCount == 0) {
    printRus(64, 90, "НЕТ ЗАДАНИЙ", C_DGRAY);
    printRus(40, 180, "ESC — закрыть", C_LGRAY);
    return;
  }
  uint8_t start = 0;
  if (boardSel >= 5)
    start = (uint8_t)(boardSel - 4);
  uint8_t shown = 0;
  for (uint8_t i = start; i < boardVisibleCount && shown < 5; i++, shown++) {
    int y = 58 + shown * 22;
    QuestCatRec &q = boardQuests[boardVisibleIdx[i]];
    uint16_t col = (i == boardSel) ? C_WHITE : C_LGRAY;
    if (i == boardSel)
      tft.fillRect(18, y - 1, SCR_W - 36, 20, C_DGRAY);
    printRus(20, y, i == boardSel ? ">" : " ", col);
    printRusStr(36, y, String(q.title), col);
  }
  printRus(20, 176, "ВНИЗ выбор  ОК взять", C_LGRAY);
  printRus(20, 192, "ESC закрыть", C_DGRAY);
}

// ─── СТРАНИЦА 5: НАСТРОЙКИ (громкость) ───
void drawPage5() {
  printRus(108, 8, "НАСТРОЙКИ", C_BORDER);
  tft.drawFastHLine(8, 28, SCR_W - 16, C_BORDER);

  printRus(12, 48, "ГРОМКОСТЬ", C_WHITE);
  drawBar(12, 72, SCR_W - 24, 22, dfVolume, 30, C_GREEN, C_YELLOW, C_ORANGE);
  char volBuf[16];
  snprintf(volBuf, sizeof(volBuf), "%u / 30", (unsigned)dfVolume);
  printRus(12, 104, volBuf, C_YELLOW);
  if (dfVolume == 0)
    printRus(12, 128, "0 = БЕЗ ЗВУКА", C_ORANGE);
  else
    printRus(12, 128, "ЗВУК ВКЛ", C_GREEN);

  printRus(12, 168, "ВНИЗ — тише", C_LGRAY);
  printRus(12, 188, "ОК — громче", C_LGRAY);
  printRus(12, 208, "ESC — назад в меню", C_DGRAY);

  drawPageIndicator();
}

void drawMenuList() {
  printRus(120, 8, "МЕНЮ", C_BORDER);
  tft.drawFastHLine(8, 28, SCR_W - 16, C_BORDER);
  const char *items[MENU_COUNT] = {"СОПРОТИВЛЕНИЯ", "ЗАДАНИЯ", "ДОСТИЖЕНИЯ",
                                   "СДАТЬСЯ",       "НАСТРОЙКИ", "UWB"};
  for (uint8_t i = 0; i < MENU_COUNT; i++) {
    int y = 40 + i * 26;
    if (menuIndex == i) {
      tft.fillRect(8, y - 2, SCR_W - 16, 24, C_DGRAY);
      printRus(16, y, ">", C_WHITE);
      printRus(32, y, items[i], C_WHITE);
    } else {
      printRus(32, y, items[i], C_LGRAY);
    }
  }
  drawPageIndicator();
}

void drawAchPage() {
  printRus(96, 8, "ДОСТИЖЕНИЯ", C_BORDER);
  tft.drawFastHLine(8, 28, SCR_W - 16, C_BORDER);
  uint8_t shown = 0;
  uint8_t skipped = 0;
  for (uint8_t id = 0; id < ACH_COUNT && shown < 8; id++) {
    if (!hasAchievement(id))
      continue;
    if (skipped < achScroll) {
      skipped++;
      continue;
    }
    int y = 40 + shown * 20;
    printRus(12, y, ACH_TABLE[id].name, C_PURPLE);
    shown++;
  }
  if (shown == 0)
    printRus(72, 80, "ПОКА ПУСТО", C_DGRAY);
  printRus(12, 210, "ВНИЗ — далее  ESC — меню", C_DGRAY);
  drawPageIndicator();
}

void drawSurrenderPage() {
  printRus(108, 8, "СДАТЬСЯ", C_BORDER);
  tft.drawFastHLine(8, 28, SCR_W - 16, C_BORDER);
  if (playerZombie)
    printRus(48, 72, "ВЫЙТИ ИЗ ЗОМБИ", C_PURPLE);
  else
    printRus(36, 72, "ДОБРОВОЛЬНАЯ СМЕРТЬ", C_RED);
  printRus(40, 110, "ДВА РАЗА ОК", C_YELLOW);
  if (surrenderStep)
    printRus(48, 148, "ЕЩЁ РАЗ ОК", C_ORANGE);
  printRus(60, 188, "ESC — отмена", C_DGRAY);
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

  if (playerZombie) {
    tft.drawRect(4, 4, SCR_W - 8, SCR_H - 8, C_PURPLE);
    printRus(96, 72, "ВЫ ЗОМБИ", C_PURPLE);
    printRus(48, 110, "ВЫХОД: СДАТЬСЯ", C_LGRAY);
    printRus(72, 140, "ДВА РАЗА ОК", C_YELLOW);
    if (surrenderStep)
      printRus(84, 168, "ЕЩЁ РАЗ ОК", C_ORANGE);
    if (eventText[0] && (millis() - eventTimeMs < 3500)) {
      tft.fillRect(8, EVT_Y - 4, SCR_W - 16, 18, C_BLACK);
      printRus(12, EVT_Y, eventText, eventColor);
    }
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
    if (menuSub < 0)
      drawMenuList();
    else if (menuSub == 0)
      drawPage2();
    else if (menuSub == 1)
      drawPage4();
    else if (menuSub == 2)
      drawAchPage();
    else if (menuSub == 3)
      drawSurrenderPage();
    else if (menuSub == 4)
      drawPage5();
    else if (menuSub == 5)
      drawPage3();
    else
      drawMenuList();
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

  if (emissionTimer == 0 && emissionDuration > 0 && ((millis() / 500) % 2))
    tft.drawRect(2, 2, SCR_W - 4, SCR_H - 4, C_RED);

  if (inAgony()) {
    if ((millis() / 400) % 2)
      tft.drawRect(6, 6, SCR_W - 12, SCR_H - 12, C_ORANGE);
    printRus(108, 12, "АГОНИЯ", C_ORANGE);
    printRus(24, 32, "АПТЕЧКА / СДАТЬСЯ", C_LGRAY);
    if (surrenderStep)
      printRus(84, 50, "ЕЩЁ РАЗ ОК", C_YELLOW);
  }

  if (chipConfirmPending && !playerZombie) {
    tft.fillRect(24, 70, SCR_W - 48, 100, C_BLACK);
    tft.drawRect(24, 70, SCR_W - 48, 100, C_YELLOW);
    printRus(48, 88, "ИСПОЛЬЗОВАТЬ?", C_YELLOW);
    printRusStr(48, 112, String(pendingChipName[0] ? pendingChipName : "ЧИП"),
                C_WHITE);
    printRus(48, 140, "ОК — да   ESC — нет", C_LGRAY);
  }

  if (boardOverlay && !playerZombie && !playerDead && !admitPending &&
      !chipConfirmPending)
    drawBoardOverlay();
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
#define DEBOUNCE_DELAY 50

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
  if (chipConfirmPending) {
    if (playerZombie || playerDead) {
      rejectPendingChip();
      btnPressed(0);
      btnPressed(1);
      btnPressed(2);
      btnPressed(3);
      return;
    }
    if (playerAgony && pendingChip.sub != CHIP_SUB_HEAL) {
      rejectPendingChip();
      btnPressed(0);
      btnPressed(1);
      btnPressed(2);
      btnPressed(3);
      return;
    }
    if (btnPressed(0)) {
      selectedRow = (selectedRow + 1) % 4;
      currentPage = 1;
      needFullRedraw = true;
    }
    if (btnPressed(2))
      confirmPendingChip();
    if (btnPressed(3))
      rejectPendingChip();
    btnPressed(1);
    return;
  }

  if (boardOverlay && !playerZombie && !playerDead && !admitPending) {
    if (btnPressed(0)) {
      if (boardVisibleCount)
        boardSel = (uint8_t)((boardSel + 1) % boardVisibleCount);
      needFullRedraw = true;
    }
    btnPressed(1);
    if (btnPressed(2)) {
      if (boardTakePending) {
        setEvent("ДОСКА: ЖДИТЕ", C_ORANGE);
      } else if (boardVisibleCount) {
        uint8_t qi = boardVisibleIdx[boardSel];
        if (writeBoardTake(boardQuests[qi].id, false))
          setEvent("ДОСКА: БЕРУ...", C_YELLOW);
        else
          setEvent("ДОСКА: ЗАНЯТО", C_ORANGE);
      }
      needFullRedraw = true;
    }
    if (btnPressed(3)) {
      boardOverlay = false;
      boardOverlayDismissed = true;
      needFullRedraw = true;
    }
    return;
  }

  /* Зомби и агония: только двойной OK = сдаться. */
  if (playerZombie || inAgony()) {
    btnPressed(0);
    btnPressed(1);
    if (btnPressed(2)) {
      if (surrenderStep == 0)
        surrenderStep = 1;
      else {
        doSurrender();
        surrenderStep = 0;
      }
      needFullRedraw = true;
    }
    if (btnPressed(3)) {
      surrenderStep = 0;
      needFullRedraw = true;
    }
    return;
  }

  if (btnPressed(0)) {
    if (currentPage == 0) {
      if (notifyCount)
        notifyIdx = (notifyIdx + 1) % notifyCount;
    } else if (currentPage == 1)
      selectedRow = (selectedRow + 1) % 4;
    else if (currentPage == 2) {
      if (menuSub < 0)
        menuIndex = (menuIndex + 1) % MENU_COUNT;
      else if (menuSub == 1 && activeTaskCount)
        questSel = (questSel + 1) % activeTaskCount;
      else if (menuSub == 2)
        achScroll = (uint8_t)(achScroll + 1);
      else if (menuSub == 4)
        adjustVolume(-2);
    }
    needFullRedraw = true;
  }
  if (btnPressed(1)) {
    if (!(currentPage == 2 && menuSub >= 0))
      currentPage = (currentPage + 1) % NUM_PAGES;
    needFullRedraw = true;
  }
  if (btnPressed(2)) {
    if (currentPage == 2) {
      if (menuSub < 0) {
        menuSub = (int8_t)menuIndex;
        surrenderStep = 0;
        achScroll = 0;
      } else if (menuSub == 3) {
        if (surrenderStep == 0)
          surrenderStep = 1;
        else {
          doSurrender();
          surrenderStep = 0;
          menuSub = -1;
        }
      } else if (menuSub == 1) {
        if (activeTaskCount && boardQuestCount && !boardTakePending) {
          if (questSel >= activeTaskCount)
            questSel = 0;
          if (writeBoardTake(activeTasks[questSel].id, true))
            setEvent("ДОСКА: СДАЮ...", C_YELLOW);
          else
            setEvent("ДОСКА: ЗАНЯТО", C_ORANGE);
        } else if (activeTaskCount) {
          if (questSel >= activeTaskCount)
            questSel = 0;
          setEvent(activeTasks[questSel].title, C_WHITE);
        }
      } else if (menuSub == 4)
        adjustVolume(2);
    }
    needFullRedraw = true;
  }
  if (btnPressed(3)) {
    if (currentPage == 2 && menuSub >= 0) {
      menuSub = -1;
      surrenderStep = 0;
    } else
      currentPage = (currentPage + NUM_PAGES - 1) % NUM_PAGES;
    needFullRedraw = true;
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
  printRus(108, 100, "PDA v2.8", C_LGRAY);

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
    esp_now_peer_info_t bcast = {};
    memcpy(bcast.peer_addr, kBcastMac, 6);
    bcast.channel = 1;
    esp_now_add_peer(&bcast);
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

  loraOk = stalkerLoraBegin();
  Serial.println(loraOk ? "[LoRa] OK" : "[LoRa] FAIL — нет Ra-01?");

  delay(2000);
  tft.fillScreen(C_BLACK);

  sessionStartMs = millis();
  lastGameTickMs = millis();
  lastDeathAtLevelUp = playerDeaths;
  grantAchievement(ACH_START_FIRST);
  bu03LastPollMs = millis();
  Serial.println("PDA v2.8 READY");
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

  if (loraOk) {
    Packet lpkt;
    char ltxt[49];
    uint8_t myLora = (uint8_t)constrain(playerEventId, 0, 255);
    if (stalkerLoraPoll(myLora, &lpkt, ltxt, sizeof(ltxt))) {
      if (ltxt[0])
        setEvent(ltxt, C_YELLOW, true);
      else
        applyIncomingPacket(lpkt, nullptr);
    }
  }

  if (pendingZoneAssign) {
    pendingZoneAssign = false;
    if (bu03Link != BU03_OFF && bu03Link != BU03_NO_LINK &&
        bu03SetCfg(pendingZoneSlot, 0)) {
      zoneSlot = pendingZoneSlot;
      memcpy(zoneAnomMac, pendingZoneMac, 6);
      lastZoneAssignMs = millis();
      sendEspNowPkt(zoneAnomMac, EMITTER_PLAYER, MSG_SLOT_READY, zoneSlot, 0, 0);
    }
  }

  if (!admitPending && !playerDead &&
      millis() - lastZoneHelloMs >= ZONE_HELLO_MS) {
    lastZoneHelloMs = millis();
    sendEspNowPkt(kBcastMac, EMITTER_PLAYER, MSG_ZONE_HELLO,
                  (int16_t)playerEventId, 0, 0);
  }

  if (zoneSlot >= 0 && millis() - lastZoneAssignMs > ZONE_SLOT_TIMEOUT_MS) {
    sendEspNowPkt(zoneAnomMac, EMITTER_PLAYER, MSG_SLOT_RELEASE, zoneSlot, 0, 0);
    zoneSlot = -1;
  }

  // ─── Обработка DAMAGE (аномалии / PvP / пси) ───
  if (newDamageReceived) {
    newDamageReceived = false;
    bool fromPlayer = incomingFromPlayer;
    bool psi = incomingControllerPsi;
    incomingFromPlayer = false;
    incomingControllerPsi = false;
    if (admitPending || playerDead || playerZombie) {
      // бой заблокирован
    } else if (fromPlayer && !psi && playerLevel < LVL_ARENA) {
      setEvent("АРЕНА С УР.12", C_ORANGE);
    } else {
    lastDamageMs = millis();
    if (!fromPlayer)
      noteAnomalyDiscovery(anomalyMac);
    else {
      arenaFights++;
      grantAchievement(ACH_ARENA_FIRST);
    }

    if (cfgFuncFlags & (1 << 0)) {
      const char *src = psi ? "PSI" : (fromPlayer ? "PVP" : "ANOMALY");
      int actualDmg = applyHpDamage(incomingDmgAmount, (uint16_t)incomingDmgMask,
                                    src, true);
      if (isAudioReady)
        myDFPlayer.playMp3Folder(1);
      if (!playerDead && !playerZombie && !playerAgony) {
        char buf[30];
        snprintf(buf, sizeof(buf), "УРОН: -%d HP", actualDmg);
        setEvent(buf, C_RED);
      }
      needFullRedraw = true;
    }

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

  if (newEmissionReceived) {
    newEmissionReceived = false;
    startEmission(incomingEmissionTimer, incomingEmissionDur);
  }

  if (newHealReceived) {
    newHealReceived = false;
    if (!isCombatLocked() && incomingHealAmount > 0) {
      applyHealAmount(incomingHealAmount, false);
      saveState();
      setEvent("ЛЕЧЕНИЕ", C_GREEN);
      needFullRedraw = true;
    }
  }

  if (newCommandReceived) {
    newCommandReceived = false;
    int16_t cmd = incomingCmd;
    int16_t v2 = incomingCmdVal2;
    if (cmd == CMD_KILL)
      markDead();
    else if (cmd == CMD_REVIVE) {
      if (playerZombie)
        setEvent("ЗОМБИ: СДАТЬСЯ", C_RED);
      else if (playerDead) {
        playerDead = false;
        playerAgony = false;
        playerHP = playerMaxHP;
        playerRad = 0;
        grantAchievement(ACH_REVIVE_FIRST);
        pulseVibro(400);
        saveState();
        setEvent("СВЯЗЬ ВОССТАНОВЛЕНА", C_GREEN);
      }
    } else if (cmd == CMD_WIPE) {
      ChipHeader wipe = {};
      wipe.sub = CHIP_ADM_RESET;
      applyAdminChip(wipe, "");
    } else if (cmd == CMD_ADD_MONEY) {
      playerMoney += v2;
      saveState();
    } else if (cmd == CMD_ADD_XP) {
      playerXP += v2;
      checkLevelUps();
      saveState();
    } else if (cmd == CMD_ADMIT) {
      admitPending = false;
      completeRegistration(nullptr);
      grantAchievement(ACH_START_FIRST);
      setEvent("ДОПУСК В ИГРУ", C_GREEN);
    }
    needFullRedraw = true;
  }

  if (newRadioReceived) {
    newRadioReceived = false;
    if (incomingRadioVol >= 0) {
      dfVolume = (uint8_t)constrain(incomingRadioVol, 0, 30);
      saveConfig();
      applyDfVolume();
    }
    if (isAudioReady && incomingRadioTrack > 0) {
      applyDfVolume();
      myDFPlayer.playMp3Folder(incomingRadioTrack);
    }
    setEvent("РАДИО", C_CYAN, true);
  }

  // ─── Обработка RADIATION ───
  if (newRadReceived) {
    newRadReceived = false;
    if (!isCombatLocked() && (cfgFuncFlags & (1 << 1))) {
      int radRes = protForType(7);
      if (isInSafeZone())
        radRes += zoneProt[7];
      radRes = min(100, radRes);
      int actualRad = incomingRadAmount * (100 - radRes) / 100;
      if (actualRad < 0)
        actualRad = 0;
      if (immuneUntilMs && millis() < immuneUntilMs)
        actualRad = 0;
      applyRadGain(actualRad);
      char rbuf[24];
      snprintf(rbuf, sizeof(rbuf), "RAD: +%d", actualRad);
      setEvent(rbuf, C_ORANGE);
      needFullRedraw = true;
    }
  }

  // ─── Обработка Safe Zone (лечение) ───
  if (newSafeZoneReceived) {
    newSafeZoneReceived = false;
    lastSafeZoneMs = millis();
    if (!isCombatLocked()) {
    if (cfgFuncFlags & (1 << 0)) {
      if (playerHP < playerMaxHP && szHealAmount > 0) {
        applyHealAmount(szHealAmount, false);
        zzHealTotal += szHealAmount;
        grantAchievement(ACH_ZZ_FIRST);
        if (zzHealTotal >= 500)
          grantAchievement(ACH_ZZ_HEAL_500);
      }
    }
    if (cfgFuncFlags & (1 << 1)) {
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

  digitalWrite(LED_GREEN, isInSafeZone() ? HIGH : LOW);

  bool detBlink = isAnomalyDetectorActive() && ((millis() / 250) % 2);
  bool dmgFlash = lastDamageMs > 0 && (millis() - lastDamageMs < 1000);
  digitalWrite(LED_RED, (dmgFlash || detBlink) ? HIGH : LOW);

  bool vibroOn = (lastDamageMs > 0 && millis() - lastDamageMs < 250) ||
                 (achVibroUntilMs > 0 && millis() < achVibroUntilMs);
  digitalWrite(PIN_VIBRO, vibroOn ? HIGH : LOW);

  // ─── Выживание без смерти ───
  if ((cfgFuncFlags & (1 << 6)) && playerDeaths == lastDeathAtLevelUp &&
      sessionStartMs > 0) {
    uint32_t alive = millis() - sessionStartMs;
    if (alive >= 7200000UL)
      grantAchievement(ACH_SURVIVE_2H);
    if (alive >= 14400000UL)
      grantAchievement(ACH_SURVIVE_4H);
    if (alive >= 28800000UL)
      grantAchievement(ACH_SURVIVE_8H);
  }

  if (millis() - lastGameTickMs >= 1000) {
    lastGameTickMs = millis();
    gameTick();
  }

  // ─── EEPROM CH0: терминалы TXN + applyChip (слоты 2/3/5/6) ~200 ms ───
  if (millis() - lastTxnPollMs >= TXN_POLL_MS) {
    lastTxnPollMs = millis();
    pollEepromTransaction();
    pollQuestBoard();
    pollChips();
  }

  // ─── BU03: опрос дистанции (~750 ms) ───
  if (bu03Link != BU03_OFF && bu03Link != BU03_NO_LINK &&
      millis() - bu03LastPollMs >= BU03_POLL_MS) {
    bu03LastPollMs = millis();
    pollBu03Distance();
    if (currentPage == 2 && menuSub == 5)
      needFullRedraw = true;
  }

  if (wantShelterUwb && zoneSlot < 0 &&
      (lastSafeZoneMs == 0 || millis() - lastSafeZoneMs > SZ_TIMEOUT_MS)) {
    wantShelterUwb = false;
    uwbInShelter = false;
  }
  if (wantShelterUwb && zoneSlot < 0 && bu03Link != BU03_OFF &&
      bu03Link != BU03_NO_LINK) {
    if (millis() - lastShelterUwbSetMs > 20000UL) {
      lastShelterUwbSetMs = millis();
      bu03SetCfg(0, 1);
    }
    if (bu03DistM >= 0.0f && bu03DistM <= (float)szRadiusM) {
      uwbInShelter = true;
      if (millis() - lastEntryOkMs >= 2000UL) {
        lastEntryOkMs = millis();
        sendEspNowPkt(shelterMac, EMITTER_PLAYER, MSG_ENTRY_OK, 0, 0, 0);
      }
    } else {
      uwbInShelter = false;
    }
  }

  // ─── Перерисовка экрана (5 FPS) ───
  if (millis() - lastDrawMs >= 200) {
    lastDrawMs = millis();
    drawScreen();
  }
}
