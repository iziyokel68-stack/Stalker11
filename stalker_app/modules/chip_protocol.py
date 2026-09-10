"""EEPROM chip header + slot rules. Mirrors proshivki/common/chip_header.h."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List, Optional, Tuple

MUX_CH_UNIVERSAL = 0
MUX_CH_RESERVED_1 = 1
MUX_CH_SLOT_1 = 2
MUX_CH_SLOT_2 = 3
MUX_CH_RESERVED_4 = 4
MUX_CH_SLOT_3 = 5
MUX_CH_SLOT_4 = 6
MUX_CH_CASHIER_CABLE = MUX_CH_UNIVERSAL
MUX_CH_SPARE_7 = 7

EQUIPMENT_CHANNELS = (MUX_CH_SLOT_1, MUX_CH_SLOT_2, MUX_CH_SLOT_3, MUX_CH_SLOT_4)

CHIP_TYPE_CONSUMABLE = 0
CHIP_TYPE_ARMOR = 1
CHIP_TYPE_ARTIFACT = 2
CHIP_TYPE_ADMIN = 3

CHIP_SUB_HEAL = 0
CHIP_SUB_ANTIRAD = 1
CHIP_SUB_REGEN = 2
CHIP_SUB_STIM = 3
CHIP_SUB_RESTORE = 4
CHIP_SUB_UPGRADE = 5

CHIP_ADM_REVIVE = 0
CHIP_ADM_MONEY = 1
CHIP_ADM_LEVEL = 2
CHIP_ADM_IMMUNITY = 3
CHIP_ADM_RESET = 4
CHIP_ADM_NEUTRALIZE = 5
CHIP_ADM_ADMIT = 6
CHIP_ADM_REGISTER = 7

CHIP_USES_INFINITE = 255
CHIP_DATA_SIZE = 0x24
CHIP_HEADER_SIZE = 0x26

LVL_CONSUMABLE = 1
LVL_STORE = 2
LVL_ARTIFACT1 = 2
LVL_ATM = 3
LVL_ARMOR = 5
LVL_ARTIFACT2 = 7
LVL_ARTIFACT3 = 10
LVL_ARENA = 12
LVL_HIDDEN_QUEST = 20
LVL_DETECTOR = 25
LVL_GLOBAL_MSG = 50

CHIP_MAX_ARMOR = 1
CHIP_MAX_ARTIFACT = 3


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


@dataclass
class ChipHeader:
    type: int = 0
    sub: int = 0
    uses: int = 1
    params: List[int] = field(default_factory=lambda: [0] * 16)
    name: str = ""

    def pack(self) -> bytes:
        buf = bytearray(CHIP_HEADER_SIZE)
        buf[0] = self.type & 0xFF
        buf[1] = self.sub & 0xFF
        buf[2] = self.uses & 0xFF
        for i, p in enumerate(self.params[:16]):
            v = int(p) & 0xFFFF
            buf[4 + i * 2] = v & 0xFF
            buf[5 + i * 2] = (v >> 8) & 0xFF
        crc = crc16_ccitt(bytes(buf[:CHIP_DATA_SIZE]))
        buf[0x24] = crc & 0xFF
        buf[0x25] = (crc >> 8) & 0xFF
        return bytes(buf)


def parse_chip(buf: bytes) -> Optional[ChipHeader]:
    if len(buf) < CHIP_HEADER_SIZE:
        return None
    stored = buf[0x24] | (buf[0x25] << 8)
    if crc16_ccitt(buf[:CHIP_DATA_SIZE]) != stored:
        return None
    params = []
    for i in range(16):
        raw = buf[4 + i * 2] | (buf[5 + i * 2] << 8)
        if raw >= 0x8000:
            raw -= 0x10000
        params.append(raw)
    return ChipHeader(type=buf[0], sub=buf[1], uses=buf[2], params=params)


def artifact_level_required(already_equipped: int) -> int:
    if already_equipped <= 0:
        return LVL_ARTIFACT1
    if already_equipped == 1:
        return LVL_ARTIFACT2
    if already_equipped == 2:
        return LVL_ARTIFACT3
    return 255


def can_use_on_channel(
    chip_type: int,
    channel: int,
    player_level: int,
    armor_count: int,
    artifact_count: int,
    admit_pending: bool = False,
    is_dead: bool = False,
    is_admin: bool = False,
) -> Tuple[bool, str]:
    """Whether the player may apply this chip on this TCA channel."""
    if channel == MUX_CH_UNIVERSAL:
        if chip_type in (CHIP_TYPE_ARMOR, CHIP_TYPE_ARTIFACT):
            return False, "ВСТАВЬ В СЛОТ 2/3/5/6"
        if chip_type == CHIP_TYPE_ADMIN or is_admin:
            return True, "OK"
        if admit_pending:
            return False, "НУЖЕН ДОПУСК"
        if is_dead:
            return False, "НУЖНО ВОСКРЕШЕНИЕ"
        if chip_type == CHIP_TYPE_CONSUMABLE:
            if player_level < LVL_CONSUMABLE:
                return False, "МАЛО УРОВНЯ"
            return True, "OK"
        return False, "НЕИЗВЕСТНЫЙ ЧИП"

    if channel not in EQUIPMENT_CHANNELS:
        return False, "НЕТ ТАКОГО СЛОТА"

    if chip_type in (CHIP_TYPE_CONSUMABLE, CHIP_TYPE_ADMIN):
        return False, "НЕ ТОТ СЛОТ — CH0"
    if admit_pending:
        return False, "НУЖЕН ДОПУСК"
    if is_dead:
        return False, "НУЖНО ВОСКРЕШЕНИЕ"

    if chip_type == CHIP_TYPE_ARMOR:
        if player_level < LVL_ARMOR:
            return False, "БРОНЯ С УР.5"
        if armor_count >= CHIP_MAX_ARMOR:
            return False, "БРОНЯ УЖЕ ЕСТЬ"
        return True, "OK"

    if chip_type == CHIP_TYPE_ARTIFACT:
        need = artifact_level_required(artifact_count)
        if need >= 255:
            return False, "ЛИМИТ АРТОВ"
        if player_level < need:
            return False, f"АРТ С УР.{need}"
        return True, "OK"

    return False, "НЕИЗВЕСТНЫЙ ЧИП"


def apply_heal(hp: int, max_hp: int, amount: int, pct: bool) -> int:
    add = (max_hp * amount) // 100 if pct else amount
    return min(max_hp, max(0, hp + add))


def apply_antirad(rad: int, amount: int, pct: bool, max_rad: int) -> int:
    sub = (max_rad * amount) // 100 if pct else amount
    return max(0, rad - sub)
