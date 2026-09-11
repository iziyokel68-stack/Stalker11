# =====================================================
# ПРОТОКОЛ v4.2 — Единый источник констант
# =====================================================
# Все ID, типы, подтипы — ТОЛЬКО здесь.
# game_logic.py и full_emulator.py импортируют отсюда.
# =====================================================

import struct
from enum import IntEnum
from dataclasses import dataclass


# =====================================================
# 1. КТО ОТПРАВЛЯЕТ (1 байт, поле emitter)
# =====================================================
class Emitter(IntEnum):
    SYSTEM    = 0   # Мастерский пульт, LoRa-ретранслятор
    PLAYER    = 1   # ПДА игрока
    ANOMALY   = 2   # Стационарная аномалия
    BASE      = 3   # Безопасная зона / укрытие


# =====================================================
# 2. ТИП СООБЩЕНИЯ (1 байт, поле msg_type)
# =====================================================
class Msg(IntEnum):
    PING       = 0
    DAMAGE     = 1   # Урон HP. val1=кол-во, val2=0, val3=dmg_mask
                     # dmg_mask: биты 0-4, 6-7 = HP-типы, бит 5 = зарезервирован (RAD — отдельно)
    HEAL       = 2   # val1=кол-во HP
    RADIATION  = 3   # val1=кол-во RAD (отдельный таймер, не зависит от HP-удара)
    # 4 — резерв (ранее MONEY, теперь через COMMAND + CmdSub.ADD_MONEY)
    COMMAND    = 5   # val1=CmdSub, val2=параметр
    ACK        = 6   # val1=текущий HP отправителя
    SAFE_ZONE  = 7   # val1=реген HP, val2=очистка RAD, val3=0 (хил-тик)
                     # val3=SZ_PROT_FLAG → пакет защиты: val1=type(0-7), val2=pct(0-100)
    EMISSION   = 8   # val1=секунд до удара, val2=длительность удара
    # 9  — резерв
    # 10 — резерв (ранее COMBO, заменён форматом DAMAGE+val3)
    RADIO      = 11  # val1=track_id, val2=volume
    ZONE_HELLO   = 20  # ПДА → аномалия, запрос слота UWB
    ZONE_ASSIGN  = 21  # аномалия → ПДА, val1=slot
    SLOT_READY   = 22  # ПДА → аномалия, tag SETCFG принят
    SLOT_RELEASE = 23  # ПДА → аномалия, выход / таймаут
    ENTRY_OK     = 24  # ПДА → убежище, вход по метрам


# =====================================================
# 3. ТИПЫ УРОНА (HP-типы, биты 0-6 dmg_mask)
# =====================================================
# БИТЫ dmg_mask (непрерывные 0-6):
#   bit 0: EXPLOSION  (взрыв)
#   bit 1: BLEED      (кровь)
#   bit 2: THERMAL    (термо)
#   bit 3: ELECTRIC   (электро)
#   bit 4: CHEMICAL   (химия)
#   bit 5: PSY        (пси)       ← сдвинут с 6-го
#   bit 6: GRAVITY    (гравитация)  ← сдвинут с 7-го
#   RADIATION — ОТДЕЛЬНЫЙ пакет Msg.RADIATION, независимый таймер.
class DmgType(IntEnum):
    EXPLOSION  = 0   # Взрыв
    BLEED      = 1   # Кровотечение
    THERMAL    = 2   # Термо (огонь)
    ELECTRIC   = 3   # Электро
    CHEMICAL   = 4   # Химия
    PSY        = 5   # Пси (сдвинут — RADIATION ушла в Msg.RADIATION)
    GRAVITY    = 6   # Гравитация (сдвинут)

# Ключи modifiers в ItemChip:
#   0-6  = защита от HP-типов (совпадает с DmgType)
#   7-9  = зарезервировано
#   10   = защита от радиации (Msg.RADIATION)
RAD_MODIFIER_KEY = 10


# Имена по проектам (индекс = DmgType, 7 элементов)
DMG_NAMES = {
    "stalker":  ["ВЗРЫВ","КРОВЬ","ТЕРМО","ЭЛЕКТРО","ХИМИЯ","ПСИ","ГРАВИТ."],
    "fallout":  ["МИНЫ","РАНЕНИЕ","ОГОНЬ","ЭНЕРГО","ТОКСИН","МЕНТАЛ","ПЛАЗМА"],
    "metro":    ["ГРАНАТА","УКУС","ОГОНЬ","ШОКЕР","ГР.ВОЗДУХ","УЖАС","АНОМ."],
    "tarkov":   ["ГРАНАТА","КРОВОТЕЧ.","ОЖОГ","ШОК","ОТРАВЛ.","КОНТУЗИЯ","ПЕРЕЛОМ"],
}


# =====================================================
# 4. ПОДТИПЫ КОМАНД (COMMAND.val1)
# =====================================================
class CmdSub(IntEnum):
    KILL              = 0   # Убить игрока
    REVIVE            = 1   # Воскрешение (heal при is_dead; не снимает admit_pending)
    WIPE_DATA         = 2   # Сброс до заводских
    ADD_MONEY         = 3   # Начислить деньги (val2=сумма)
    SET_PROJECT       = 4   # Сменить проект (val2=project_id)
    ADD_XP            = 5   # Начислить опыт (val2=кол-во)
    GRANT_RESISTANCE  = 6   # Увеличить сопротивление (val2=DmgType, модификатор в чипе)
    ADMIT             = 7   # Допуск в игру (сессия; снимает admit_pending; только главный мастер)


# =====================================================
# 5. ТИПЫ ЧИПОВ (EEPROM, байт 0x00 на физическом чипе)
# =====================================================
class ChipType(IntEnum):
    MEDKIT      = 0   # Аптечка (value=HP)
    ANTIRAD     = 1   # Антирад (value=RAD)
    ARMOR       = 2   # Броня (modifiers=резисты, value=бонус HP)
    ARTIFACT    = 3   # Артефакт (modifiers=резисты, value=реген)
    CONSUMABLE  = 4   # Прочие расходники (value=эффект)
    RESISTANCE  = 5   # Чип сопротивления (квестовый)
    COMMAND     = 6   # Админ-команда (value=CmdSub)

CHIP_NAMES = {
    0: "АПТЕЧКА",
    1: "АНТИРАД",
    2: "БРОНЯ",
    3: "АРТЕФАКТ",
    4: "РАСХОДНИК",
    5: "СОПРОТИВЛ",
    6: "КОМАНДА",
}

# Подтипы команд для чипов — совпадают с CmdSub
# ChipType.COMMAND + value=CmdSub.ADMIT  → допуск в игру (сессия, главный мастер)
# ChipType.COMMAND + value=CmdSub.REVIVE → воскрешение (любой мастер, при is_dead)
# EEPROM type=3 sub=6 → ДОПУСК В ИГРУ; sub=0 → ВОСКРЕШЕНИЕ
# ChipType.COMMAND + value=CmdSub.ADD_MONEY + modifiers["amount"]=500 → +500 RUB


# =====================================================
# 6. МУЛЬТИПРОЕКТНОСТЬ
# =====================================================
PROJECTS = {
    0: {"name": "S.T.A.L.K.E.R.", "dmg_names": DMG_NAMES["stalker"]},
    1: {"name": "Fallout",        "dmg_names": DMG_NAMES["fallout"]},
    2: {"name": "Metro",          "dmg_names": DMG_NAMES["metro"]},
    3: {"name": "Tarkov",         "dmg_names": DMG_NAMES["tarkov"]},
}


# =====================================================
# 7. ПАКЕТ (8 байт, Little-Endian)
# =====================================================
@dataclass
class Packet:
    """
    Протокол v4.1 (8 байт).
    [1B emitter] [1B msg_type] [2B val1] [2B val2] [2B val3]
    Все int16 — signed, Little-Endian.
    """
    emitter:  int = Emitter.SYSTEM
    msg_type: int = Msg.PING
    val1:     int = 0
    val2:     int = 0
    val3:     int = 0

    FORMAT = '<BBhhh'
    SIZE = 8

    def pack(self) -> bytes:
        return struct.pack(self.FORMAT, self.emitter, self.msg_type,
                           self.val1, self.val2, self.val3)

    @classmethod
    def unpack(cls, data: bytes) -> 'Packet':
        if len(data) != cls.SIZE:
            raise ValueError(f"Bad packet size: {len(data)}")
        emitter, msg_type, val1, val2, val3 = struct.unpack(cls.FORMAT, data)
        return cls(emitter, msg_type, val1, val2, val3)


# =====================================================
# 8. УТИЛИТЫ СОЗДАНИЯ ПАКЕТОВ
# =====================================================
def create_damage_packet(amount: int, dmg_type: int = DmgType.EXPLOSION) -> Packet:
    """One-type HP packet (legacy, val3=0)."""
    return Packet(Emitter.ANOMALY, Msg.DAMAGE, val1=amount, val2=dmg_type, val3=0)

def create_damage_multi_packet(amount: int, dmg_mask: int) -> Packet:
    """Multi-type HP packet (val3=dmg_mask, бит 5 зарезервирован)."""
    return Packet(Emitter.ANOMALY, Msg.DAMAGE, val1=amount, val2=0, val3=dmg_mask)

def create_radiation_packet(rad_amount: int) -> Packet:
    """RAD таймер: бьёт независимо от HP-удара."""
    return Packet(Emitter.ANOMALY, Msg.RADIATION, val1=rad_amount)

def create_heal_packet(amount: int) -> Packet:
    return Packet(Emitter.SYSTEM, Msg.HEAL, val1=amount)

def create_safezone_packet(regen_hp: int = 0, cleanse_rad: int = 0) -> Packet:
    """HP-тик + RAD-очистка. val3=0 — хил-тик."""
    return Packet(Emitter.BASE, Msg.SAFE_ZONE, val1=regen_hp, val2=cleanse_rad, val3=0)

# Маркер пакета защиты зоны (отличает от хил-тика)
SZ_PROT_FLAG: int = 0x7FFF

def create_zone_prot_packet(dmg_type: int, pct: int) -> Packet:
    """Пакет защиты зоны: val1=type(0-7), val2=pct(0-100), val3=SZ_PROT_FLAG."""
    return Packet(Emitter.BASE, Msg.SAFE_ZONE,
                  val1=dmg_type, val2=max(0, min(100, pct)), val3=SZ_PROT_FLAG)

def create_emission_packet(timer_sec: int, duration_sec: int) -> Packet:
    return Packet(Emitter.SYSTEM, Msg.EMISSION, val1=timer_sec, val2=duration_sec)

def create_command_packet(cmd: int, param: int = 0) -> Packet:
    return Packet(Emitter.SYSTEM, Msg.COMMAND, val1=cmd, val2=param)
