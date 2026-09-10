# =====================================================
# СИМУЛЯТОР ПДА — Игровая логика
# =====================================================
# Этот файл содержит ВСЮ логику игры, отделённую от железа.
# При портировании на Arduino — переносится почти 1 в 1.
# Все константы — из protocol.py (единый источник).
# =====================================================

import time
from typing import List, Optional, Dict, Union, Set
from protocol import ChipType, CmdSub, CHIP_NAMES, PROJECTS, DmgType, RAD_MODIFIER_KEY

# Обратная совместимость
PROJECT_THEMES = PROJECTS

# =====================================================
# ШКАЛА СТАТОВ — batch 3: внутр. HP/RAD/урон в единицах STAT_SCALE
# На TFT — только полоса заполнения (%), без цифр HP/RAD.
# =====================================================
STAT_SCALE = 1000
EMISSION_DMG_PER_TICK = 250   # было 25 при шкале 100
EMISSION_RAD_PER_TICK = 250
RAD_SICKNESS_TICK_DMG = 10    # было 1 HP/мин при шкале 100

# =====================================================
# ПРОГРЕССИЯ — hardware/PROGRESSION.txt §2–3, §7
# =====================================================
MAX_LEVEL = 100

RANK_TIERS = [
    {"min_level": 1,  "title": "НОВИЧОК",  "rub": 0,    "discount": 0},
    {"min_level": 10, "title": "БРОДЯГА",  "rub": 800,  "discount": 3},
    {"min_level": 25, "title": "СТАЛКЕР",  "rub": 1500, "discount": 6},
    {"min_level": 40, "title": "ВЕТЕРАН",  "rub": 2500, "discount": 10},
    {"min_level": 60, "title": "МАСТЕР",   "rub": 4000, "discount": 14},
    {"min_level": 80, "title": "ЛЕГЕНДА",  "rub": 6000, "discount": 18},
]

# Доп. условия ранга (коды достижений); None = только мин. уровень
RANK_PREREQ_ACH = [
    None,
    ["reg_complete"],
    None,
    ["quest_1"],
    None,
    ["quest_15"],
]

ACHIEVEMENTS: Dict[str, Dict[str, Union[str, int]]] = {
    "start_first":    {"name": "Первый контакт",      "xp": 20,   "rub": 50},
    "reg_complete":   {"name": "Регистрация",         "xp": 80,   "rub": 150},
    "level_2":        {"name": "Допуск к Зоне",       "xp": 40,   "rub": 100},
    "anomaly_1":      {"name": "Первый след",         "xp": 150,  "rub": 200},
    "anomaly_5":      {"name": "Разведчик I",         "xp": 200,  "rub": 300},
    "anomaly_10":     {"name": "Разведчик II",        "xp": 350,  "rub": 500},
    "anomaly_15":     {"name": "Разведчик III",       "xp": 500,  "rub": 700},
    "anomaly_20":     {"name": "Разведчик IV",        "xp": 700,  "rub": 1000},
    "anomaly_30":     {"name": "Картограф Зоны",      "xp": 1000, "rub": 1500},
    "anomaly_50":     {"name": "Охотник за аномалиями", "xp": 1500, "rub": 2500},
    "dmg_100":        {"name": "Лёгкий укус",         "xp": 50,   "rub": 80},
    "dmg_500":        {"name": "Закалённый I",        "xp": 150,  "rub": 200},
    "dmg_2000":       {"name": "Закалённый II",       "xp": 400,  "rub": 600},
    "exit_alive":     {"name": "Выход живым",         "xp": 60,   "rub": 100},
    "anticheat_10":   {"name": "Честный сталкер",     "xp": 100,  "rub": 150},
    "rad_50":         {"name": "Фон накоплен",        "xp": 40,   "rub": 50},
    "rad_sick":       {"name": "Лучевая болезнь",     "xp": 80,   "rub": 0},
    "rad_clean":      {"name": "Чистый",              "xp": 120,  "rub": 200},
    "antirad_10":     {"name": "Антирад-опыт",        "xp": 100,  "rub": 150},
    "buy_first":      {"name": "Первый заказ",        "xp": 60,   "rub": 0},
    "spend_1k":       {"name": "Покупатель I",        "xp": 100,  "rub": 100},
    "spend_5k":       {"name": "Покупатель II",       "xp": 250,  "rub": 300},
    "spend_20k":      {"name": "Покупатель III",      "xp": 500,  "rub": 700},
    "spend_50k":      {"name": "Покупатель IV",       "xp": 800,  "rub": 1200},
    "spend_100k":     {"name": "Меценат Зоны",        "xp": 1200, "rub": 2000},
    "sell_first":     {"name": "Первая сделка",       "xp": 60,   "rub": 0},
    "earn_2k":        {"name": "Торговец I",          "xp": 150,  "rub": 150},
    "earn_10k":       {"name": "Торговец II",         "xp": 350,  "rub": 400},
    "earn_50k":       {"name": "Торговец III",        "xp": 700,  "rub": 800},
    "earn_200k":      {"name": "Барон рынка",         "xp": 1500, "rub": 2500},
    "quest_1":        {"name": "Первое поручение",    "xp": 100,  "rub": 300},
    "quest_5":        {"name": "Исполнитель I",       "xp": 200,  "rub": 500},
    "quest_15":       {"name": "Исполнитель II",      "xp": 400,  "rub": 1000},
    "quest_30":       {"name": "Исполнитель III",     "xp": 750,  "rub": 2000},
    "quest_50":       {"name": "Легенда заданий",     "xp": 1250, "rub": 4000},
    "hidden_1":       {"name": "Теневой след",        "xp": 175,  "rub": 500},
    "hidden_10":      {"name": "Куратор тайн",        "xp": 600,  "rub": 1800},
    "art_first":      {"name": "Первый артефакт",     "xp": 120,  "rub": 180},
    "art_types_3":    {"name": "Коллекционер I",      "xp": 250,  "rub": 400},
    "art_types_10":   {"name": "Коллекционер II",     "xp": 600,  "rub": 900},
    "armor_first":    {"name": "Броненосец",          "xp": 100,  "rub": 150},
    "full_loadout":   {"name": "Полный комплект",     "xp": 300,  "rub": 500},
    "slots_all":      {"name": "Мастер слотов",       "xp": 400,  "rub": 600},
    "level_10":       {"name": "Десятка",             "xp": 150,  "rub": 200},
    "level_25":       {"name": "Четверть века",       "xp": 300,  "rub": 400},
    "level_50":       {"name": "Полпути",             "xp": 600,  "rub": 800},
    "level_75":       {"name": "Старожил",            "xp": 1000, "rub": 1200},
    "level_100":      {"name": "Хроникёр Зоны",       "xp": 2000, "rub": 5000},
    "death_first":    {"name": "Связь потеряна",     "xp": 0,    "rub": 0},
    "revive_first":   {"name": "Второе дыхание",      "xp": 50,   "rub": 100},
    "survive_2h":     {"name": "Дежурство",           "xp": 25,   "rub": 50},
    "survive_4h":     {"name": "Марафонец",           "xp": 80,   "rub": 120},
    "survive_8h":     {"name": "Длинная вылазка",     "xp": 150,  "rub": 250},
    "deaths_5_live":  {"name": "Упорный",             "xp": 100,  "rub": 200},
    "transfer_first": {"name": "Перевод",             "xp": 80,   "rub": 0},
    "bank_10k":       {"name": "Банкир I",            "xp": 200,  "rub": 250},
    "transfer_5k":    {"name": "Щедрый",              "xp": 150,  "rub": 0},
    "arena_first":    {"name": "Первый бой",          "xp": 150,  "rub": 200},
    "arena_win_5":    {"name": "Победитель I",        "xp": 400,  "rub": 600},
    "arena_win_20":   {"name": "Победитель II",       "xp": 1000, "rub": 1500},
    "arena_win_50":   {"name": "Чемпион",             "xp": 2500, "rub": 5000},
    "zz_first":       {"name": "Убежище",             "xp": 40,   "rub": 50},
    "zz_heal_500":    {"name": "Лечебница",           "xp": 150,  "rub": 200},
    "detector_first": {"name": "Нюх на аномалию",     "xp": 100,  "rub": 150},
    "detector_50":    {"name": "Проводник",           "xp": 300,  "rub": 400},
    "res_plus_1":     {"name": "Шрамы Зоны",          "xp": 80,   "rub": 100},
    "res_10":         {"name": "Каменная кожа",       "xp": 200,  "rub": 300},
    "res_50_cap":     {"name": "Предел боли",         "xp": 500,  "rub": 800},
    "stalker_kraft":  {"name": "Сталкеркрафт",        "xp": 800,  "rub": 1000},
    "monolith_path":  {"name": "Путь Монолита",       "xp": 1500, "rub": 3000},
    "zero_deaths_day":{"name": "Ни шагу назад",       "xp": 200,  "rub": 400},
}

LEVEL_ACH_MAP = {2: "level_2", 10: "level_10", 25: "level_25", 50: "level_50", 75: "level_75", 100: "level_100"}
QUEST_COUNT_ACH = {1: "quest_1", 5: "quest_5", 15: "quest_15", 30: "quest_30", 50: "quest_50"}
HIDDEN_QUEST_ACH = {1: "hidden_1", 10: "hidden_10"}
ANOMALY_COUNT_ACH = {1: "anomaly_1", 5: "anomaly_5", 10: "anomaly_10", 15: "anomaly_15", 20: "anomaly_20", 30: "anomaly_30", 50: "anomaly_50"}
DMG_TOTAL_ACH = {100: "dmg_100", 500: "dmg_500", 2000: "dmg_2000"}
LVL_HIDDEN_QUEST = 20
LVL_ARENA = 12
ZOMBIE_NOTIFY_TYPES = {"broadcast", "admin", "emission", "radio"}
LETHAL_ZOMBIE_SOURCES = ("EMISSION_STRIKE", "controller_psi")


def xp_per_level(lvl: int) -> int:
    """XP для перехода L → L+1. Цели: ур.10 за ~1–3 ч активной игры, ур.100 за ~6–7 игровых дней."""
    if lvl < 10:
        return 90 + lvl * 32
    if lvl < 40:
        return 200 + (lvl - 9) * 8
    if lvl < 70:
        return 420 + (lvl - 39) * 4
    return 460 + (lvl - 69) * 3


def xp_total_for_level(target_level: int) -> int:
    return sum(xp_per_level(l) for l in range(1, target_level))


def rub_reward_on_level_up(lvl: int) -> int:
    if 1 <= lvl <= 4:
        return 500
    if lvl == 5:
        return 2000
    if 6 <= lvl <= 9:
        return 500
    if 10 <= lvl <= 12:
        return 1000
    if lvl % 5 == 0:
        return 1000
    return 500


class ActiveTask:
    """Активное задание (квест-чип CH0). Минимальная модель для EEPROM/симулятора."""

    def __init__(self, task_id: str, title: str, short_desc: str, status: str = "active",
                 rub_reward: int = 0, hidden: bool = False):
        self.id = task_id
        self.title = title
        self.short_desc = short_desc
        self.status = status
        self.rub_reward = max(0, int(rub_reward))
        self.hidden = bool(hidden)

    def to_dict(self) -> dict:
        return {
            "id": self.id, "title": self.title, "short_desc": self.short_desc,
            "status": self.status, "rub_reward": self.rub_reward,
        }

    @classmethod
    def from_dict(cls, d: dict) -> "ActiveTask":
        return cls(
            d.get("id", "?"), d.get("title", "Задание"),
            d.get("short_desc", ""), d.get("status", "active"),
            d.get("rub_reward", 0),
        )


class ItemChip:
    """Данные на EEPROM чипе. Типы — из protocol.ChipType."""

    # Прямые ссылки на ChipType (не дублируем числа)
    TYPE_MEDKIT     = ChipType.MEDKIT       # 0
    TYPE_ANTIRAD    = ChipType.ANTIRAD      # 1
    TYPE_ARMOR      = ChipType.ARMOR        # 2
    TYPE_ARTIFACT   = ChipType.ARTIFACT     # 3
    TYPE_CONSUMABLE = ChipType.CONSUMABLE   # 4
    TYPE_RESISTANCE = ChipType.RESISTANCE   # 5
    TYPE_COMMAND    = ChipType.COMMAND       # 6

    # Подтипы команд — ссылки на CmdSub
    CMD_KILL        = CmdSub.KILL           # 0
    CMD_REVIVE      = CmdSub.REVIVE         # 1
    CMD_WIPE        = CmdSub.WIPE_DATA      # 2
    CMD_MONEY       = CmdSub.ADD_MONEY      # 3
    CMD_SET_PROJECT = CmdSub.SET_PROJECT    # 4
    CMD_ADD_XP      = CmdSub.ADD_XP         # 5
    CMD_GRANT_RES   = CmdSub.GRANT_RESISTANCE  # 6
    CMD_ADMIT       = CmdSub.ADMIT          # 7

    TYPE_NAMES = CHIP_NAMES

    def __init__(self, item_type=0, value=0, used=False, modifiers=None, uses=1):
        self.item_type = item_type
        self.value = value
        self.used = used
        self.uses = uses       # Прочность: кол-во зарядов (255=∞)
        # modifiers: {DmgType_id: %защиты, "hp": бонус_HP}
        self.modifiers = modifiers if modifiers is not None else {}

    def get_name(self):
        name = self.TYPE_NAMES.get(self.item_type, "???")
        if self.uses != 255 and self.uses > 0:
            name += f"({self.uses})"
        return name


class Player:
    """Все данные и механики игрока"""

    # Порог накопленного урона для +1% собственного сопротивления (в ед. STAT_SCALE)
    RESISTANCE_THRESHOLD = STAT_SCALE
    # Максимум собственного сопротивления (%)
    RESISTANCE_CAP = 50

    def __init__(self, save_file="pda_state.json"):
        self.save_file = save_file
        
        # Основные статы (будут загружены из файла, если он есть)
        self.max_health = STAT_SCALE
        self.health = STAT_SCALE
        self.max_rad = STAT_SCALE
        self.radiation = 0       # 0..max_rad (на экране — % полосы)
        self.money = 1000
        self.xp = 0
        self.level = 1
        self.death_counter = 0   # Счетчик смертей
        self.is_dead = False
        self.is_zombie = False   # Роль зомби (RAD 100%, выброс, пси контроллера)
        self.in_agony = False    # После HP=0 от обычного урона; ещё не is_dead
        self.admit_pending = True  # Ожидание чипа мастера; НЕ сохраняется в NVS
        self.project_id = 0      # ID текущего проекта (0=Stalker, 1=Fallout...)
        self.player_name = ""    # Имя сталкера (NVS), задаётся при регистрации через ПК

        # Новая архитектура слотов (5 штук, как в ТЗ):
        self.slots: List[Optional[ItemChip]] = [None, None, None, None, None]
        
        # Таймер для радиации (счетчик секунд)
        self.rad_tick_counter = 0

        # Invulnerability Frame (MAC_АДРЕС -> время последнего удара)
        self.last_damage_time: Dict[str, float] = {}

        # === АНТИЧИТ (счётчик экранирования, без урона HP) ===
        self.in_anomaly_zone = False
        self.last_packet_time = 0.0
        self.rssi_history: List[int] = []
        self.cheat_shield_count = 0       # Секретный счётчик; мастер смотрит admin/serial
        self.ANTIPHASE_TIMEOUT = 3.5
        self.RSSI_SAFE_EXIT = -85

        # === СОБСТВЕННЫЕ СОПРОТИВЛЕНИЯ (накопительные) ===
        # Ключ: ID HP-типа (0-6 = бит в dmg_mask)
        # 0=Взрыв, 1=Кровь, 2=Термо, 3=Электро,
        # 4=Химия, 5=ПСИ, 6=ГРАВИТ.
        # 7-9 = зарезервировано для будущих HP-типов
        # 10 = RAD (защита от радиации, отдельный ключ modifiers)
        self.innate_resistance: Dict[int, float] = {
            0: 0.0, 1: 0.0, 2: 0.0, 3: 0.0, 4: 0.0, 5: 0.0, 6: 0.0
        }
        # Аккумулятор HP-урона по типам (0-6)
        self.damage_taken_accumulator: Dict[int, float] = {
            0: 0.0, 1: 0.0, 2: 0.0, 3: 0.0, 4: 0.0, 5: 0.0, 6: 0.0
        }

        # === ОТДЕЛЬНОЕ РАД-СОПРОТИВЛЕНИЕ ===
        self.rad_resistance: float   = 0.0   # % защиты от радиации (0-50)
        self.rad_accumulator: float  = 0.0   # накопленная радиация для порога

        # Отдельный кулдаун для RAD-пакетов (MAC → время)
        self.last_radiation_time: Dict[str, float] = {}

        # === СИСТЕМА УВЕДОМЛЕНИЙ ===
        self.notifications: List[Dict[str, Union[str, float, int]]] = []
        self.current_note_idx = 0

        # === ПРОГРЕССИЯ ===
        self.rank = 0
        self.achievements_unlocked: Set[str] = set()
        self.active_tasks: List[ActiveTask] = []
        self.selected_task_idx = 0
        self.registered = False
        self.rank_ready_notified: Set[int] = set()
        self.anomaly_sources: Set[str] = set()
        self.quests_completed = 0
        self.hidden_quests_completed = 0
        self.actor_role = 0
        self.total_hp_damage_taken = 0.0
        self._had_first_admit = False
        
        # === ПЕРЕМЕННЫЕ ВЫБРОСА ===
        self.emission_timer = -1
        self.emission_duration = 0
        self.last_safe_zone_time = 0.0
        self.emission_strike_tick = 0
        self.sz_emission_protect = False

        # === ЗАЩИТА ЗОНЫ (временная, пока в зоне) ===
        # Индекс 0-6 = HP-типы, 7 = RAD
        self.zone_prot: Dict[int, float] = {i: 0.0 for i in range(8)}

        # Пытаемся загрузить сохранение
        self.load_state()
        self.on_power_on()

    def on_power_on(self):
        """Каждое включение ПДА — нужен допуск в игру (главный мастер). is_dead сохраняется."""
        self.admit_pending = True

    @property
    def needs_registration(self) -> bool:
        """Первое включение без записи имени в NVS — экран регистрации."""
        return not self.registered

    def is_system_locked(self) -> bool:
        """Блокировка всей системы ПДА (регистрация или ожидание допуска)."""
        return self.needs_registration or self.admit_pending

    def is_play_locked(self) -> bool:
        """Алиас is_system_locked."""
        return self.is_system_locked()

    def is_session_locked(self) -> bool:
        """Алиас is_system_locked."""
        return self.is_system_locked()

    def is_combat_locked(self) -> bool:
        """Игровые эффекты — при lock, смерти или роли зомби."""
        return self.is_system_locked() or self.is_dead or self.is_zombie

    def is_in_agony(self) -> bool:
        """Агония — состояние после потери всех HP, не порог 10%."""
        return (
            self.in_agony
            and not self.is_dead
            and not self.is_zombie
            and not self.is_system_locked()
        )

    def _mark_agony(self):
        self.health = 0
        self.in_agony = True
        self.is_dead = False
        self.is_zombie = False
        self.save_state()
        return {"type": "agony", "text": "АГОНИЯ"}

    def _clear_agony(self):
        if not self.in_agony:
            return
        self.in_agony = False
        self.save_state()

    def _resolve_zero_hp(self, source_id: str):
        self.health = 0
        if source_id in LETHAL_ZOMBIE_SOURCES:
            return self._mark_zombie()
        if self.in_agony:
            self._mark_dead()
            return {"type": "death", "text": "СТАЛКЕР ПОГИБ"}
        return self._mark_agony()

    def start_emission(self, timer_sec: int, duration_sec: int):
        self.emission_timer = max(0, int(timer_sec))
        self.emission_duration = max(0, int(duration_sec))
        self.emission_strike_tick = 0
        return self.add_notification("ВЫБРОС!", "emission", priority=2, admin=True)

    def grant_session_admit(self):
        """Чип ДОПУСК В ИГРУ / CmdSub.ADMIT — сессионный вход (главный мастер)."""
        if self.needs_registration:
            return {"type": "error", "text": "НУЖНА РЕГИСТРАЦИЯ"}
        if not self.admit_pending:
            return {"type": "error", "text": "ДОПУСК УЖЕ ЕСТЬ"}
        self.admit_pending = False
        if not self.needs_registration and not self._had_first_admit:
            self._had_first_admit = True
            self.grant_achievement("start_first")
        self.save_state()
        self._check_rank_eligibility()
        if self.is_dead:
            return {"type": "info", "text": "ДОПУСК В ЗОНУ"}
        return {"type": "info", "text": "ДОПУСК В ЗОНУ"}

    def register_player(self, name: str):
        """Регистрация через ПК/EEPROM (programmer.py) — имя в NVS, затем ожидание допуска."""
        name = (name or "").strip()[:24]
        if not name:
            return {"type": "error", "text": "ПУСТОЕ ИМЯ"}
        if self.registered:
            return {"type": "error", "text": "УЖЕ ЗАРЕГИСТР."}
        self.player_name = name
        self.registered = True
        if self.level < 2:
            self.level = 2
        self.grant_achievement("reg_complete")
        self.save_state()
        return {"type": "info", "text": f"РЕГ: {name}"}

    def grant_revive(self):
        """Чип ВОСКРЕШЕНИЕ / CmdSub.REVIVE — heal при смерти (любой мастер)."""
        if self.admit_pending or self.needs_registration:
            return {"type": "error", "text": "НУЖЕН ДОПУСК В ИГРУ"}
        if self.is_zombie:
            return {"type": "error", "text": "РЕЖИМ ЗОМБИ"}
        if not self.is_dead:
            return {"type": "error", "text": "ИГРОК ЖИВ"}
        self.is_dead = False
        self.in_agony = False
        self.health = self.max_health
        self.radiation = 0
        self.save_state()
        return {"type": "revive", "text": "СВЯЗЬ ВОССТАНОВЛЕНА"}

    def surrender(self):
        """Добровольная сдача из меню ПДА (п.4) — единственный выход из роли зомби."""
        if self.needs_registration or self.admit_pending:
            return {"type": "error", "text": "НЕДОСТУПНО"}
        if self.is_zombie:
            self.is_zombie = False
            self.in_agony = False
            self._mark_dead()
            return {"type": "death", "text": "СВЯЗЬ ПОТЕРЯНА"}
        if self.is_dead:
            return {"type": "error", "text": "УЖЕ МЁРТВ"}
        self.in_agony = False
        self._mark_dead()
        return {"type": "death", "text": "СВЯЗЬ ПОТЕРЯНА"}

    def save_state(self):
        """
        Сохранение состояния игрока во внутреннюю память.
        
        Симулятор: JSON файл.
        ESP32: NVS (Non-Volatile Storage). Маппинг ключей:
          NVS namespace: "player"
          "hp"       -> int16  (health)
          "hp_max"   -> int16  (max_health)  
          "rad"      -> int16  (radiation, 0..max_rad, шкала STAT_SCALE)
          "rad_max"  -> int16  (max_rad)
          "money"    -> int32  (money)
          "xp"       -> int32  (xp)
          "lvl"      -> int8   (level)
          "deaths"   -> int16  (death_counter)
          "dead"     -> int8   (is_dead, 0/1)
          "res_0".."res_6"   -> int8  (innate_resistance HP-типы 0-6, 0-50%)
          "acc_0".."acc_6"   -> int16 (damage_taken_accumulator HP-типы 0-6)
          "rad_res"          -> int8  (rad_resistance, 0-50%)
          "rad_acc"          -> int16 (rad_accumulator)
        
        ВАЖНО: save_state() вызывается при КАЖДОМ изменении HP, смерти, XP.
        На ESP32 NVS выдерживает ~100к циклов записи, этого хватит на тысячи игр.
        """
        import json
        # Конвертируем ключи в строки для JSON
        innate_res_str = {str(k): v for k, v in self.innate_resistance.items()}
        dmg_acc_str = {str(k): v for k, v in self.damage_taken_accumulator.items()}
        state = {
            "health": self.health,
            "max_health": self.max_health,
            "radiation": self.radiation,
            "max_rad": self.max_rad,
            "money": self.money,
            "xp": self.xp,
            "level": self.level,
            "death_counter": self.death_counter,
            "is_dead": self.is_dead,
            "innate_resistance": innate_res_str,
            "damage_taken_accumulator": dmg_acc_str,
            "rad_resistance": self.rad_resistance,
            "rad_accumulator": self.rad_accumulator,
            "emission_timer": self.emission_timer,
            "emission_duration": self.emission_duration,
            "notifications": self.notifications,
            "project_id": self.project_id,
            "rank": self.rank,
            "achievements": list(self.achievements_unlocked),
            "active_tasks": [t.to_dict() for t in self.active_tasks],
            "registered": self.registered,
            "player_name": self.player_name,
            "is_zombie": self.is_zombie,
            "in_agony": self.in_agony,
            "cheat_shield_count": self.cheat_shield_count,
            "quests_completed": self.quests_completed,
            "anomaly_sources": list(self.anomaly_sources),
            "total_hp_damage_taken": self.total_hp_damage_taken,
            "rank_ready_notified": list(self.rank_ready_notified),
            "had_first_admit": self._had_first_admit,
        }
        try:
            with open(self.save_file, "w") as f:
                json.dump(state, f)
        except Exception as e:
            print(f"Ошибка сохранения: {e}")

    def load_state(self):
        """Загрузка состояния из файла"""
        import json
        import os
        if os.path.exists(self.save_file):
            try:
                with open(self.save_file, "r") as f:
                    state = json.load(f)
                    self.health = state.get("health", STAT_SCALE)
                    self.max_health = state.get("max_health", STAT_SCALE)
                    self.max_rad = state.get("max_rad", STAT_SCALE)
                    self.radiation = state.get("radiation", 0)
                    self.money = state.get("money", 1000)
                    self.xp = state.get("xp", 0)
                    self.level = state.get("level", 1)
                    self.death_counter = state.get("death_counter", 0)
                    self.is_dead = state.get("is_dead", False)
                    # Загрузка собственных сопротивлений
                    saved_res = state.get("innate_resistance", {})
                    for k, v in saved_res.items():
                        key = int(k)
                        self.innate_resistance[key] = v
                    saved_acc = state.get("damage_taken_accumulator", {})
                    for k, v in saved_acc.items():
                        key = int(k)
                        if key in self.damage_taken_accumulator:  # только ключи 0-6
                            self.damage_taken_accumulator[key] = v
                    self.rad_resistance = state.get("rad_resistance", 0.0)
                    self.rad_accumulator = state.get("rad_accumulator", 0.0)
                    self.emission_timer = state.get("emission_timer", -1)
                    self.emission_duration = state.get("emission_duration", 0)
                    self.notifications = state.get("notifications", [])
                    self.project_id = state.get("project_id", 0)
                    self.rank = state.get("rank", 0)
                    self.achievements_unlocked = set(state.get("achievements", []))
                    self.active_tasks = [ActiveTask.from_dict(t) for t in state.get("active_tasks", [])]
                    self.registered = state.get("registered", False)
                    self.player_name = state.get("player_name", "")
                    self.is_zombie = state.get("is_zombie", False)
                    self.in_agony = state.get("in_agony", False)
                    if self.is_dead or self.is_zombie:
                        self.in_agony = False
                    elif self.health <= 0:
                        self.in_agony = True
                    self.cheat_shield_count = state.get("cheat_shield_count", 0)
                    self.quests_completed = state.get("quests_completed", 0)
                    self.anomaly_sources = set(state.get("anomaly_sources", []))
                    self.total_hp_damage_taken = state.get("total_hp_damage_taken", 0.0)
                    self.rank_ready_notified = set(state.get("rank_ready_notified", []))
                    self._had_first_admit = state.get("had_first_admit", False)
            except:
                pass

    def _mark_dead(self):
        self.health = 0
        self.is_dead = True
        self.is_zombie = False
        self.in_agony = False
        self.death_counter += 1
        self.grant_achievement("death_first")
        self.save_state()

    def _mark_zombie(self):
        """Переход в роль игрока-зомби (не обычное воскрешение)."""
        self.health = 0
        self.is_dead = False
        self.is_zombie = True
        self.in_agony = False
        self.death_counter += 1
        self.save_state()
        return {"type": "zombie", "text": "ВЫ ЗОМБИ"}

    def apply_damage(self, dmg: float, dmg_type: int = 0, source_id: str = "unknown", rssi: int = -70):
        """Получение урона с учетом надетой брони и артефактов"""
        if self.is_combat_locked():
            return None
        if self.in_agony:
            return self._resolve_zero_hp(source_id)

        # Защита от дублирующихся пакетов (урон не чаще раза в секунду от ОДНОГО источника)
        # Если это радиационный "тик" (dmg_type == -1), пускаем без задержки.
        t = time.time()
        if dmg_type != -1 and source_id != "anticheat_sys":
            last_t = self.last_damage_time.get(source_id, 0.0)
            if (t - last_t < 0.9):
                return None
            self.last_damage_time[source_id] = t
            
            # Обновляем данные для античита
            self.in_anomaly_zone = True
            self.last_packet_time = t
            self.rssi_history.append(rssi)
            if len(self.rssi_history) > 5: self.rssi_history.pop(0)
            self._note_anomaly_source(source_id)

        # Суммируем защиту: экипировка + собственные сопротивления + зона
        equip_res_pct = 0.0
        for slot in self.slots:
            if slot and slot.modifiers:
                equip_res_pct += slot.modifiers.get(dmg_type, 0.0)

        innate_res_pct = self.innate_resistance.get(dmg_type, 0.0) if dmg_type >= 0 else 0.0
        zone_res_pct   = self.zone_prot.get(dmg_type, 0.0) if dmg_type >= 0 and self.is_in_safe_zone() else 0.0
        total_res_pct  = equip_res_pct + innate_res_pct + zone_res_pct

        total_res_pct = min(100.0, max(0.0, total_res_pct))
        actual_damage = dmg * (1.0 - (total_res_pct / 100.0))
        actual_damage = round(actual_damage)

        if dmg_type >= 0 and dmg_type in self.damage_taken_accumulator:
            self.damage_taken_accumulator[dmg_type] += actual_damage
            while self.damage_taken_accumulator[dmg_type] >= self.RESISTANCE_THRESHOLD:
                self.damage_taken_accumulator[dmg_type] -= self.RESISTANCE_THRESHOLD
                if self.innate_resistance[dmg_type] < self.RESISTANCE_CAP:
                    self.innate_resistance[dmg_type] += 1.0

        if actual_damage > 0 and dmg_type != -1:
            self.total_hp_damage_taken += actual_damage
            self._check_damage_achievements()

        self.health -= actual_damage
        if self.health <= 0:
            return self._resolve_zero_hp(source_id)

        self.save_state()
        return {"type": "damage", "text": f"УРОН: -{actual_damage} HP", "value": actual_damage}

    def apply_damage_multi(self, dmg: float, dmg_mask: int,
                           source_id: str = "unknown", rssi: int = -70):
        """
        Обработка DAMAGE-пакета (val3=dmg_mask, val2=0).
        РАДИАЦИЯ приходит отдельным Мсг.RADIATION и обрабатывается в apply_radiation().
        Бит 5 dmg_mask зарезервирован и игнорируется.
        """
        if self.is_combat_locked():
            return None
        if self.in_agony:
            return self._resolve_zero_hp(source_id)
        if dmg_mask == 0:
            return None

        t = time.time()
        last_t = self.last_damage_time.get(source_id, 0.0)
        if (t - last_t < 0.9):
            return None
        self.last_damage_time[source_id] = t
        self.in_anomaly_zone = True
        self.last_packet_time = t
        self.rssi_history.append(rssi)
        if len(self.rssi_history) > 5: self.rssi_history.pop(0)
        self._note_anomaly_source(source_id)

        events = []

        # HP урон по маске (бит 5 игнорируется — RAD приходит Msg.RADIATION)
        if dmg_mask != 0:
            total_damage = 0.0
            types_hit = []
            for i in range(8):
                if not (dmg_mask & (1 << i)):
                    continue
                equip_res = 0.0
                for slot in self.slots:
                    if slot and slot.modifiers:
                        equip_res += slot.modifiers.get(i, 0.0)
                innate_res = self.innate_resistance.get(i, 0.0)
                total_res = min(100.0, max(0.0, equip_res + innate_res))
                dmg_i = round(dmg * (1.0 - total_res / 100.0))
                total_damage += dmg_i
                types_hit.append(i)
                if i in self.damage_taken_accumulator:
                    self.damage_taken_accumulator[i] += dmg_i
                    while self.damage_taken_accumulator[i] >= self.RESISTANCE_THRESHOLD:
                        self.damage_taken_accumulator[i] -= self.RESISTANCE_THRESHOLD
                        if self.innate_resistance[i] < self.RESISTANCE_CAP:
                            self.innate_resistance[i] += 1.0

            total_damage = round(total_damage)
            if total_damage > 0:
                self.total_hp_damage_taken += total_damage
                self._check_damage_achievements()
            self.health -= total_damage
            if self.health <= 0:
                return self._resolve_zero_hp(source_id)
            n = len(types_hit)
            suffix = f" ({n} типа)" if n > 1 else ""
            events.append({"type": "damage", "text": f"УРОН: -{total_damage} HP{suffix}", "value": total_damage})

        self.save_state()
        return events[-1] if events else None



    def apply_heal(self, amount):
        """Лечение (в т.ч. выход из агонии). Не работает при смерти/зомби."""
        if self.is_combat_locked():
            return None
        self.health = min(self.health + amount, self.max_health)
        if self.in_agony and self.health > 0:
            self._clear_agony()
        self.save_state()
        return {"type": "heal", "text": f"ЛЕЧЕНИЕ: +{amount} HP"}

    def apply_zone_prot(self, dmg_type: int, pct: float):
        """Cохранить защиту зоны (0-7). Активна пока is_in_safe_zone()."""
        if 0 <= dmg_type <= 7:
            self.zone_prot[dmg_type] = max(0.0, min(100.0, float(pct)))

    def apply_radiation(self, rad: float):
        """Накопление радиации (от Msg.RADIATION).
        Использует отдельное поле rad_resistance (не HP-бит)."""
        if self.is_combat_locked():
            return None

        # Защита: экипировка (modifiers[RAD_MODIFIER_KEY=10]) + накопленное rad_resistance
        rad_res_pct = 0.0
        for slot in self.slots:
            if slot and slot.modifiers:
                rad_res_pct += slot.modifiers.get(RAD_MODIFIER_KEY, 0.0)
        rad_res_pct += self.rad_resistance
        # + защита зоны (zone_prot[7])
        if self.is_in_safe_zone():
            rad_res_pct += self.zone_prot.get(7, 0.0)

        rad_res_pct = min(100.0, max(0.0, rad_res_pct))
        actual_rad = rad * (1.0 - (rad_res_pct / 100.0))

        # Накопительная устойчивость к радиации
        if actual_rad > 0:
            self.rad_accumulator += actual_rad
            while self.rad_accumulator >= self.RESISTANCE_THRESHOLD:
                self.rad_accumulator -= self.RESISTANCE_THRESHOLD
                if self.rad_resistance < self.RESISTANCE_CAP:
                    self.rad_resistance += 1.0
        actual_rad = round(actual_rad)

        self.radiation = min(self.radiation + actual_rad, self.max_rad)
        events = [{"type": "radiation", "text": f"РАДИАЦИЯ: +{actual_rad}"}]

        if self.radiation >= self.max_rad:
            events.append(self._mark_zombie())

        self.save_state()  # Сохраняем радиацию и аккумулятор сопротивлений
        return events

    def tick(self):
        """
        Метод должен вызываться раз в секунду.
        Обрабатывает периодические эффекты и античит.
        """
        if self.is_system_locked():
            return None

        # 1. Проверка античита (RSSI-базированая)
        t = time.time()
        if self.in_anomaly_zone and (t - self.last_packet_time > self.ANTIPHASE_TIMEOUT):
            # Сигнал пропал. Проверяем последний RSSI.
            last_rssi = self.rssi_history[-1] if self.rssi_history else -100
            self.in_anomaly_zone = False # Сбрасываем статус
            
            # Резкий обрыв сильного сигнала — экранирование; только счётчик, без урона HP
            if last_rssi > self.RSSI_SAFE_EXIT:
                self.cheat_shield_count += 1
                self.save_state()
                return {"type": "info", "text": "СИГНАЛ ПРЕРВАН"}

        # 2. Обработка Выброса (Emission) — предупреждения идут и зомби (admin broadcast)
        if self.emission_timer > 0:
            self.emission_timer -= 1
            # Дискретные уведомления на экран
            thresholds = {
                3600: "ВНИМАНИЕ: Выброс через 1 час",
                1800: "ВНИМАНИЕ: Выброс через 30 минут",
                900:  "ВНИМАНИЕ: Выброс через 15 минут",
                300:  "ВНИМАНИЕ: Выброс через 5 минут",
            }
            if self.emission_timer in thresholds:
                return self.add_notification(
                    thresholds[self.emission_timer], "emission", priority=2, admin=True
                )

        elif self.emission_timer == 0:
            if self.emission_duration > 0:
                self.emission_duration -= 1
                self.emission_strike_tick += 1
                
                # Проверка укрытия (Зелёной Зоны)
                is_safe = (t - self.last_safe_zone_time < 5.0) and getattr(
                    self, "sz_emission_protect", False
                )
                
                if not is_safe and not self.is_dead and not self.is_zombie:
                    # Каждые 10 секунд — урон (агония: удар → зомби)
                    if self.emission_strike_tick % 10 == 0:
                        self.apply_damage(EMISSION_DMG_PER_TICK, 6, source_id="EMISSION_STRIKE")
                        if not self.is_zombie:
                            self.apply_radiation(EMISSION_RAD_PER_TICK)
            else:
                self.emission_timer = -1 # Выброс завершен
                self.emission_strike_tick = 0
                return self.add_notification("ВЫБРОС ОКОНЧЕН", "emission", priority=2, admin=True)

        if self.is_combat_locked() or self.in_agony:
            return None

        # 3. Стадия 2: Лучевая болезнь (>50% от max_rad)
        if self.radiation > self.max_rad * 0.5:
            self.grant_achievement("rad_50")
            self.grant_achievement("rad_sick")
            self.rad_tick_counter += 1
            if self.rad_tick_counter >= 60:  # Раз в 60 секунд (мин)
                self.rad_tick_counter = 0
                return self.apply_damage(RAD_SICKNESS_TICK_DMG, dmg_type=-1) 
        else:
            self.rad_tick_counter = 0

        return None

    def apply_antirad(self, amount):
        """Снижение радиации"""
        if self.is_combat_locked():
            return None
        self.radiation = max(self.radiation - amount, 0)
        self.save_state()
        return {"type": "antirad", "text": f"АНТИРАД: -{amount} RAD"}

    def add_money(self, amount):
        """Начисление денег"""
        self.money += amount
        self.save_state()
        return {"type": "money", "text": f"+{amount} RUB"}

    def spend_money(self, amount):
        """Трата денег"""
        if self.is_zombie or self.in_agony or self.is_dead:
            return False
        if self.money >= amount:
            self.money -= amount
            self.save_state()
            return True
        return False

    def use_consumable(self, item: ItemChip):
        """Использовать расходный чип (аптечка, антирад, XP)"""
        if item.used:
            return {"type": "error", "text": "ЧИП ИСПОЛЬЗОВАН!"}
        # Админ-команда: работает при ожидании допуска / смерти
        if item.item_type == ItemChip.TYPE_COMMAND:
            if item.uses != 255:
                item.uses -= 1
                if item.uses <= 0: item.used = True
            cmd = item.value
            if cmd == ItemChip.CMD_ADMIT:
                return self.grant_session_admit()
            elif cmd == ItemChip.CMD_REVIVE:
                return self.grant_revive()
            elif cmd == ItemChip.CMD_MONEY:
                amount = item.modifiers.get("amount", 0)
                self.money += amount
                self.save_state()
                return {"type": "money", "text": f"+{amount} RUB"}
            elif cmd == ItemChip.CMD_SET_PROJECT:
                pid = item.modifiers.get("pid", 0)
                if pid in PROJECT_THEMES:
                    self.project_id = pid
                    self.save_state()
                    return {"type": "info", "text": f"ПРОЕКТ: {PROJECT_THEMES[pid]['name']}"}
                return {"type": "error", "text": "НЕИЗВЕСТНЫЙ ПРОЕКТ"}
            elif cmd == ItemChip.CMD_ADD_XP:
                xp = item.modifiers.get("amount", item.value)
                self.xp += xp
                self._check_level_up()
                self.save_state()
                return {"type": "xp", "text": f"+{xp} XP"}
            elif item.modifiers.get("rank_confirm"):
                return self.confirm_next_rank()
            return {"type": "error", "text": "НЕИЗВЕСТНАЯ КОМАНДА"}
        if self.admit_pending:
            return {"type": "error", "text": "НУЖЕН ДОПУСК В ИГРУ"}
        if self.is_zombie:
            return {"type": "error", "text": "ЗОМБИ: СДАТЬСЯ"}
        if self.is_dead:
            return {"type": "error", "text": "НУЖНО ВОСКРЕШЕНИЕ"}
        if self.in_agony and item.item_type != ItemChip.TYPE_MEDKIT and not item.modifiers.get("quest"):
            return {"type": "error", "text": "АГОНИЯ"}
        if item.modifiers.get("quest"):
            if self.is_zombie or self.in_agony:
                return {"type": "error", "text": "ЗОМБИ: СДАТЬСЯ" if self.is_zombie else "АГОНИЯ"}
            return self._use_quest_chip(item)
        # Прочность (uses): 255=∞, иначе декремент
        if item.uses != 255:
            item.uses -= 1
            if item.uses <= 0:
                item.used = True
        if item.item_type == ItemChip.TYPE_MEDKIT:
            return self.apply_heal(item.value)
        elif item.item_type == ItemChip.TYPE_ANTIRAD:
            return self.apply_antirad(item.value)
        elif item.item_type == ItemChip.TYPE_RESISTANCE:
            for dmg_type, pct in item.modifiers.items():
                if isinstance(dmg_type, int):
                    return self.grant_resistance(dmg_type, pct)
            return {"type": "error", "text": "ПУСТОЙ ЧИП"}
        return {"type": "error", "text": "НЕИЗВЕСТНЫЙ ЧИП"}

    def insert_artifact(self, slot_index, item: ItemChip):
        """Вставить чип в слот экипировки. Слоты универсальные (как CH2/3/5/6):
        тип берётся с чипа, не с номера слота. Слот 1 симулятора = расходник CH0."""
        if slot_index < 0 or slot_index >= len(self.slots):
            return {"type": "error", "text": "НЕТ ТАКОГО СЛОТА"}

        if self.is_zombie or self.in_agony or self.is_dead:
            return {"type": "error", "text": "ЗОМБИ: СДАТЬСЯ" if self.is_zombie else ("АГОНИЯ" if self.in_agony else "НУЖНО ВОСКРЕШЕНИЕ")}

        if slot_index == 1:
            return {"type": "error", "text": "РАСХОДНИК — СЛОТ CH0"}

        if self.slots[slot_index] is not None:
            return {"type": "error", "text": "СЛОТ ЗАНЯТ!"}

        armor_count = sum(1 for s in self.slots if s and s.item_type == ItemChip.TYPE_ARMOR)
        art_count = sum(1 for s in self.slots if s and s.item_type == ItemChip.TYPE_ARTIFACT)

        if item.item_type == ItemChip.TYPE_ARMOR:
            if self.level < 5:
                return {"type": "error", "text": "БРОНЯ С УР.5"}
            if armor_count >= 1:
                return {"type": "error", "text": "БРОНЯ УЖЕ ЕСТЬ"}
        elif item.item_type == ItemChip.TYPE_ARTIFACT:
            need = 2 if art_count <= 0 else 7 if art_count == 1 else 10 if art_count == 2 else 255
            if need >= 255:
                return {"type": "error", "text": "ЛИМИТ АРТОВ"}
            if self.level < need:
                return {"type": "error", "text": f"АРТ С УР.{need}"}

        self.slots[slot_index] = item

        # Из чипа можно читать бонусное ХП
        bonus_hp = 0
        if item.modifiers:
            bonus_hp = item.modifiers.get("hp", 0)

        if item.item_type == ItemChip.TYPE_ARMOR:
            self.max_health += (item.value + bonus_hp)
            self.grant_achievement("armor_first")
            self.save_state()
            return {"type": "armor", "text": f"БРОНЯ: +{item.value + bonus_hp} MAX HP"}
        elif item.item_type == ItemChip.TYPE_ARTIFACT:
            self.max_health += bonus_hp
            self.health += bonus_hp // 2
            self.grant_achievement("art_first")
            self.save_state()
            return {"type": "artifact", "text": f"АРТЕФАКТ АКТИВЕН"}

        return {"type": "info", "text": "ЧИП УСТАНОВЛЕН"}

    def remove_artifact(self, slot_index):
        """Вытащить чип"""
        if slot_index < 0 or slot_index >= len(self.slots):
            return None
        item = self.slots[slot_index]
        if item is None:
            return None

        bonus_hp = 0
        if item.modifiers:
            bonus_hp = item.modifiers.get("hp", 0)

        # Снимаем бонусы ХП
        if item.item_type in (ItemChip.TYPE_ARMOR, ItemChip.TYPE_ARTIFACT):
            self.max_health -= (item.value + bonus_hp)
            if self.health > self.max_health:
                self.health = self.max_health

        self.slots[slot_index] = None
        return {"type": "info", "text": f"ЧИП ИЗВЛЕЧЁН"}

    def _check_level_up(self):
        """Проверка повышения уровня (PROGRESSION.txt §2)."""
        leveled = False
        while self.level < MAX_LEVEL and self.xp >= xp_total_for_level(self.level + 1):
            self.level += 1
            reward = rub_reward_on_level_up(self.level)
            self.money += reward
            leveled = True
            code = LEVEL_ACH_MAP.get(self.level)
            if code:
                self.grant_achievement(code)
            self.add_notification(f"УРОВЕНЬ {self.level} +{reward} RUB", "level_up", priority=1)
        if leveled:
            self._check_rank_eligibility()
        return leveled

    def get_level_name(self):
        if self.level >= 100:
            return "Хроникёр Зоны"
        names = {1: "Новичок", 2: "Бродяга", 3: "Сталкер",
                 4: "Ветеран", 5: "Мастер", 6: "Легенда"}
        bracket = min(self.level, 6)
        return names.get(bracket, "Сталкер")

    def get_rank_title(self) -> str:
        if 0 <= self.rank < len(RANK_TIERS):
            return RANK_TIERS[self.rank]["title"]
        return "???"

    def has_achievement(self, code: str) -> bool:
        return code in self.achievements_unlocked

    def grant_achievement(self, code: str) -> Optional[Dict]:
        if code not in ACHIEVEMENTS or self.has_achievement(code):
            return None
        ach = ACHIEVEMENTS[code]
        self.achievements_unlocked.add(code)
        xp = int(ach["xp"])
        rub = int(ach["rub"])
        if xp:
            self.xp += xp
            self._check_level_up()
        if rub:
            self.money += rub
        text = f"ДОСТИЖЕНИЕ: {ach['name']}"
        if xp:
            text += f" +{xp}XP"
        if rub:
            text += f" +{rub}RUB"
        evt = self.add_notification(text, "achievement", priority=3)
        self.save_state()
        return evt

    def _note_anomaly_source(self, source_id: str):
        if source_id in ("unknown", "anticheat_sys", "EMISSION_STRIKE"):
            return
        if source_id in self.anomaly_sources:
            return
        self.anomaly_sources.add(source_id)
        n = len(self.anomaly_sources)
        for threshold, code in sorted(ANOMALY_COUNT_ACH.items()):
            if n >= threshold:
                self.grant_achievement(code)

    def _check_damage_achievements(self):
        for threshold, code in sorted(DMG_TOTAL_ACH.items()):
            if self.total_hp_damage_taken >= threshold:
                self.grant_achievement(code)

    def _rank_prereqs_met(self, next_rank: int) -> bool:
        reqs = RANK_PREREQ_ACH[next_rank] if next_rank < len(RANK_PREREQ_ACH) else None
        if not reqs:
            return True
        return all(self.has_achievement(r) for r in reqs)

    def is_rank_eligible(self, next_rank: Optional[int] = None) -> bool:
        if next_rank is None:
            next_rank = self.rank + 1
        if next_rank >= len(RANK_TIERS):
            return False
        tier = RANK_TIERS[next_rank]
        return self.level >= tier["min_level"] and self._rank_prereqs_met(next_rank)

    def _check_rank_eligibility(self) -> Optional[Dict]:
        next_rank = self.rank + 1
        if next_rank >= len(RANK_TIERS):
            return None
        if not self.is_rank_eligible(next_rank):
            return None
        if next_rank in self.rank_ready_notified:
            return None
        self.rank_ready_notified.add(next_rank)
        title = RANK_TIERS[next_rank]["title"]
        text = f"РАНГ ДОСТУПЕН: {title}"
        return self.add_notification(text, "rank_ready", priority=2)

    def confirm_next_rank(self) -> Dict:
        """Подтверждение ранга мастером (CONFIG:RANK_CONFIRM)."""
        next_rank = self.rank + 1
        if not self.is_rank_eligible(next_rank):
            return {"type": "error", "text": "РАНГ НЕДОСТУПЕН"}
        self.rank = next_rank
        tier = RANK_TIERS[self.rank]
        self.money += int(tier["rub"])
        text = f"РАНГ: {tier['title']} +{tier['rub']}RUB {tier['discount']}%"
        self.add_notification(text, "rank_confirmed", priority=2)
        self._check_rank_eligibility()
        self.save_state()
        return {"type": "info", "text": text}

    def _use_quest_chip(self, item: ItemChip) -> Dict:
        mods = item.modifiers
        hidden = bool(mods.get("hidden"))
        if hidden and self.level < LVL_HIDDEN_QUEST:
            return {"type": "error", "text": "СКРЫТЫЙ ЛВ20"}
        if mods.get("complete"):
            qid = mods.get("quest_id", "")
            return self.complete_quest(qid, hidden=hidden)
        qid = str(mods.get("quest_id", f"q{int(time.time())}"))
        title = mods.get("title", "Задание")
        desc = mods.get("short_desc", "Поручение мастера")
        for t in self.active_tasks:
            if t.id == qid and t.status == "active":
                return {"type": "error", "text": "ЗАДАНИЕ УЖЕ АКТИВНО"}
        rub = max(0, int(mods.get("rub_reward", 0)))
        self.active_tasks.append(
            ActiveTask(qid, title, desc, "active", rub_reward=rub, hidden=hidden)
        )
        if len(self.active_tasks) > 8:
            self.active_tasks.pop(0)
        self.save_state()
        return {"type": "info", "text": f"ЗАДАНИЕ: {title}"}

    def complete_quest(self, quest_id: str, hidden: bool = False) -> Dict:
        task = None
        for t in self.active_tasks:
            if t.id == quest_id and t.status == "active":
                task = t
                break
        if not task:
            return {"type": "error", "text": "НЕТ ЗАДАНИЯ"}
        task.status = "completed"
        self.quests_completed += 1
        if hidden or getattr(task, "hidden", False):
            self.hidden_quests_completed += 1
            for threshold, code in sorted(HIDDEN_QUEST_ACH.items()):
                if self.hidden_quests_completed >= threshold:
                    self.grant_achievement(code)
        rub = max(0, int(getattr(task, "rub_reward", 0)))
        if rub:
            self.money += rub
            self.xp += rub
            self._check_level_up()
        for threshold, code in sorted(QUEST_COUNT_ACH.items()):
            if self.quests_completed >= threshold:
                self.grant_achievement(code)
        self._check_rank_eligibility()
        self.save_state()
        text = f"ВЫПОЛНЕНО: {task.title}"
        if rub:
            text += f" +{rub}RUB +{rub}XP"
        return {"type": "info", "text": text}

    def get_active_tasks(self) -> List[ActiveTask]:
        return [t for t in self.active_tasks if t.status == "active"]

    def add_notification(self, text: str, ntype: str = "info", priority: int = 0, admin: bool = False):
        """Добавить уведомление в историю (макс 5). type: achievement, rank_ready, info…
        Зомби видит только общие оповещения мастера (broadcast / emission / radio)."""
        if self.is_zombie and not admin and ntype not in ZOMBIE_NOTIFY_TYPES:
            return {"type": ntype, "text": text, "notify": False}
        self.notifications.append({
            "text": text,
            "time": time.time(),
            "type": ntype,
            "priority": priority,
        })
        if len(self.notifications) > 5:
            self.notifications.pop(0)
        self.current_note_idx = len(self.notifications) - 1
        return {"type": ntype, "text": text, "notify": True}

    def get_display_notification(self) -> Optional[Dict]:
        """Уведомление с наивысшим приоритетом среди «свежих» (10 с)."""
        if not self.notifications:
            return None
        now = time.time()
        lmc = getattr(self, "last_manual_cycle_time", 0.0)
        fresh = [n for n in self.notifications
                 if (now - n["time"] < 10) or (now - lmc < 10)]
        if not fresh:
            return None
        return max(fresh, key=lambda n: (n.get("priority", 0), n["time"]))

    def cycle_notifications(self):
        """Переключить отображаемое уведомление"""
        if not self.notifications:
            return
        self.current_note_idx = (self.current_note_idx - 1) % len(self.notifications)

    def is_in_safe_zone(self) -> bool:
        """Находится ли игрок в укрытии (Зелёной Зоне) прямо сейчас"""
        return (time.time() - self.last_safe_zone_time < 5.0)
