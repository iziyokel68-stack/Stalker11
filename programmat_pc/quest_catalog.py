"""Каталог доски заданий (EEPROM CH0 @0x0100) и правила выдачи."""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import List, Optional

QUEST_CAT_BASE = 0x0100
QUEST_CAT_FULL_BASE = 0x0800
QUEST_CAT_HDR_SIZE = 8
QUEST_CAT_REC_SIZE = 48
QUEST_CAT_MAX = 24
QUEST_CAT_MAGIC = b"QS"
QUEST_CAT_VERSION = 1

QUEST_TAKE_BASE = 0x00A4
QUEST_TAKE_SIZE = 24

QUEST_MODE_TIMEOUT = 0
QUEST_MODE_ONESHOT = 1
QUEST_MODE_SHARED = 2
QUEST_TIMEOUT_DEFAULT = 120

QUEST_CLAIM_FREE = 0
QUEST_CLAIM_HELD = 1
QUEST_CLAIM_DONE = 2
QUEST_CLAIM_GONE = 3

MODE_NAMES = {
    "timeout": QUEST_MODE_TIMEOUT,
    "oneshot": QUEST_MODE_ONESHOT,
    "shared": QUEST_MODE_SHARED,
}
MODE_FROM_INT = {v: k for k, v in MODE_NAMES.items()}


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


@dataclass
class QuestCard:
    code: str
    title: str
    rub: int = 0
    hidden: bool = False
    mode: str = "timeout"
    timeout_min: int = QUEST_TIMEOUT_DEFAULT
    body: str = ""

    @property
    def mode_id(self) -> int:
        return MODE_NAMES.get((self.mode or "timeout").strip().lower(), QUEST_MODE_TIMEOUT)


def pack_flags(hidden: bool, mode_id: int) -> int:
    return (1 if hidden else 0) | ((mode_id & 3) << 1)


def unpack_mode(flags: int) -> int:
    return (flags >> 1) & 3


def pack_catalog(cards: List[QuestCard]) -> bytes:
    cards = cards[:QUEST_CAT_MAX]
    recs = b""
    for q in cards:
        code = (q.code or "")[:8].encode("utf-8", "replace")
        title = (q.title or "")[:24].encode("utf-8", "replace")
        rec = bytearray(QUEST_CAT_REC_SIZE)
        rec[0:len(code)] = code
        rec[8:8 + len(title)] = title
        rec[32:36] = struct.pack("<i", int(q.rub))
        rec[36] = pack_flags(bool(q.hidden), q.mode_id)
        tmin = int(q.timeout_min or QUEST_TIMEOUT_DEFAULT)
        rec[37:39] = struct.pack("<H", tmin)
        recs += bytes(rec)
    crc = crc16_ccitt(recs)
    hdr = bytearray(QUEST_CAT_HDR_SIZE)
    hdr[0:2] = QUEST_CAT_MAGIC
    hdr[2] = QUEST_CAT_VERSION
    hdr[3] = len(cards)
    hdr[4:6] = struct.pack("<H", crc)
    return bytes(hdr) + recs


def unpack_catalog(blob: bytes) -> List[QuestCard]:
    if len(blob) < QUEST_CAT_HDR_SIZE or blob[0:2] != QUEST_CAT_MAGIC:
        return []
    if blob[2] != QUEST_CAT_VERSION:
        return []
    count = blob[3]
    recs = blob[QUEST_CAT_HDR_SIZE:QUEST_CAT_HDR_SIZE + count * QUEST_CAT_REC_SIZE]
    if len(recs) < count * QUEST_CAT_REC_SIZE:
        return []
    crc = struct.unpack_from("<H", blob, 4)[0]
    if crc16_ccitt(recs) != crc:
        return []
    out = []
    for i in range(count):
        r = recs[i * QUEST_CAT_REC_SIZE:(i + 1) * QUEST_CAT_REC_SIZE]
        code = r[0:8].split(b"\x00", 1)[0].decode("utf-8", "replace")
        title = r[8:32].split(b"\x00", 1)[0].decode("utf-8", "replace")
        rub = struct.unpack_from("<i", r, 32)[0]
        flags = r[36]
        tmin = struct.unpack_from("<H", r, 37)[0] or QUEST_TIMEOUT_DEFAULT
        out.append(QuestCard(
            code=code,
            title=title,
            rub=rub,
            hidden=bool(flags & 1),
            mode=MODE_FROM_INT.get(unpack_mode(flags), "timeout"),
            timeout_min=tmin,
        ))
    return out


def quest_is_listed(mode: str, claim: int, elapsed_min: int,
                    timeout_min: int = QUEST_TIMEOUT_DEFAULT) -> bool:
    """Видно ли задание на доске (можно взять)."""
    mid = MODE_NAMES.get((mode or "timeout").lower(), QUEST_MODE_TIMEOUT)
    if mid == QUEST_MODE_SHARED:
        return True
    if claim in (QUEST_CLAIM_DONE, QUEST_CLAIM_GONE):
        return False
    if claim == QUEST_CLAIM_FREE:
        return True
    if claim == QUEST_CLAIM_HELD:
        if mid == QUEST_MODE_ONESHOT:
            return False
        return elapsed_min >= (timeout_min or QUEST_TIMEOUT_DEFAULT)
    return True


def format_quest_add(card: QuestCard) -> str:
    """Serial-команда QUEST_ADD для терминала (title — последний ключ)."""
    title = (card.title or "").replace("\n", " ").replace("\r", " ")
    code = (card.code or "")[:8].replace(",", " ").replace(":", " ")
    mode = (card.mode or "timeout").strip().lower()
    if mode not in MODE_NAMES:
        mode = "timeout"
    tmin = int(card.timeout_min or QUEST_TIMEOUT_DEFAULT)
    hidden = 1 if card.hidden else 0
    return (
        f"QUEST_ADD:id={code},rub={int(card.rub)},mode={mode},"
        f"timeout_min={tmin},hidden={hidden},title={title}"
    )


def next_claim_status(mode: str, claim: int, elapsed_min: int,
                      timeout_min: int = QUEST_TIMEOUT_DEFAULT,
                      completed: bool = False) -> int:
    """Новый статус слота выдачи после тика или сдачи."""
    mid = MODE_NAMES.get((mode or "timeout").lower(), QUEST_MODE_TIMEOUT)
    if completed:
        if mid == QUEST_MODE_SHARED:
            return QUEST_CLAIM_FREE
        return QUEST_CLAIM_DONE
    if claim != QUEST_CLAIM_HELD:
        return claim
    if elapsed_min < (timeout_min or QUEST_TIMEOUT_DEFAULT):
        return QUEST_CLAIM_HELD
    if mid == QUEST_MODE_ONESHOT:
        return QUEST_CLAIM_GONE
    return QUEST_CLAIM_FREE
