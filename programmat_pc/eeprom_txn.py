"""
S.T.A.L.K.E.R. — EEPROM terminal↔PDA transaction protocol v1 (Python mirror).

Region 0x0080..0x00A3 on 24LC256; full map in proshivki/common/eeprom_protocol.h
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from typing import Optional

TXN_EEPROM_BASE = 0x0080
TXN_BLOCK_SIZE = 36
TXN_DATA_SIZE = 34
TXN_MAGIC = b"ST"
TXN_VERSION = 1

TXN_STATE_IDLE = 0
TXN_STATE_PENDING = 1
TXN_STATE_PROCESSING = 2
TXN_STATE_SUCCESS = 3
TXN_STATE_FAILED = 4

TXN_OP_PURCHASE = 0
TXN_OP_BANK = 1
TXN_OP_QUEST = 2
TXN_OP_ADMIT = 3
TXN_OP_ATM = 4

TXN_FLAG_DISCOUNT = 0x01
TXN_FLAG_BANK_DEPOSIT = 0x02
TXN_FLAG_QUEST_COMPLETE = 0x04

TXN_RESULT_OK = 0
TXN_RESULT_INSUFFICIENT_FUNDS = 1
TXN_RESULT_LEVEL_TOO_LOW = 2
TXN_RESULT_SYSTEM_LOCKED = 3
TXN_RESULT_BAD_CRC = 4
TXN_RESULT_BAD_MAGIC = 5
TXN_RESULT_QUEST_NOT_FOUND = 6
TXN_RESULT_QUEST_DUPLICATE = 7
TXN_RESULT_NOT_IMPLEMENTED = 8

TXN_OFF_RESERVED = 26
TXN_RESERVED_LEN = 8

TXN_OP_NAMES = {
    TXN_OP_PURCHASE: "PURCHASE",
    TXN_OP_BANK: "BANK",
    TXN_OP_QUEST: "QUEST",
    TXN_OP_ADMIT: "ADMIT",
    TXN_OP_ATM: "ATM",
}


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc


@dataclass
class TxnBlock:
    state: int = TXN_STATE_IDLE
    txn_id: int = 0
    amount_rub: int = 0
    item_id: int = 0
    op_type: int = TXN_OP_PURCHASE
    flags: int = 0
    balance_after: int = 0
    result_code: int = 0
    amount_paid: int = 0
    reserved: bytes = field(default_factory=lambda: bytes(TXN_RESERVED_LEN))

    def pack(self, state: Optional[int] = None) -> bytes:
        buf = bytearray(TXN_BLOCK_SIZE)
        buf[0:2] = TXN_MAGIC
        buf[2] = TXN_VERSION
        buf[3] = self.state if state is None else state
        struct.pack_into("<I", buf, 4, self.txn_id & 0xFFFFFFFF)
        struct.pack_into("<i", buf, 8, self.amount_rub)
        struct.pack_into("<H", buf, 12, self.item_id & 0xFFFF)
        buf[14] = self.op_type
        buf[15] = self.flags
        struct.pack_into("<i", buf, 16, self.balance_after)
        struct.pack_into("<H", buf, 20, self.result_code & 0xFFFF)
        struct.pack_into("<i", buf, 22, self.amount_paid)
        res = self.reserved[:TXN_RESERVED_LEN].ljust(TXN_RESERVED_LEN, b"\x00")
        buf[TXN_OFF_RESERVED : TXN_OFF_RESERVED + TXN_RESERVED_LEN] = res
        crc = crc16_ccitt(bytes(buf[:TXN_DATA_SIZE]))
        struct.pack_into("<H", buf, 34, crc)
        return bytes(buf)

    @classmethod
    def unpack(cls, data: bytes) -> "TxnBlock":
        if len(data) < TXN_BLOCK_SIZE:
            raise ValueError("block too short")
        if data[0:2] != TXN_MAGIC:
            raise ValueError("bad magic")
        if data[2] != TXN_VERSION:
            raise ValueError("bad version")
        stored_crc = struct.unpack_from("<H", data, 34)[0]
        calc_crc = crc16_ccitt(data[:TXN_DATA_SIZE])
        if stored_crc != calc_crc:
            raise ValueError("CRC mismatch")
        return cls(
            state=data[3],
            txn_id=struct.unpack_from("<I", data, 4)[0],
            amount_rub=struct.unpack_from("<i", data, 8)[0],
            item_id=struct.unpack_from("<H", data, 12)[0],
            op_type=data[14],
            flags=data[15],
            balance_after=struct.unpack_from("<i", data, 16)[0],
            result_code=struct.unpack_from("<H", data, 20)[0],
            amount_paid=struct.unpack_from("<i", data, 22)[0],
            reserved=bytes(data[TXN_OFF_RESERVED : TXN_OFF_RESERVED + TXN_RESERVED_LEN]),
        )

    @classmethod
    def build_purchase(cls, txn_id: int, amount_rub: int, item_id: int = 0) -> "TxnBlock":
        return cls(
            state=TXN_STATE_IDLE,
            txn_id=txn_id,
            amount_rub=amount_rub,
            item_id=item_id,
            op_type=TXN_OP_PURCHASE,
        )

    @classmethod
    def build_bank(
        cls, txn_id: int, amount_rub: int, account_code: int = 0, deposit: bool = True
    ) -> "TxnBlock":
        return cls(
            state=TXN_STATE_IDLE,
            txn_id=txn_id,
            amount_rub=amount_rub,
            item_id=account_code,
            op_type=TXN_OP_BANK,
            flags=TXN_FLAG_BANK_DEPOSIT if deposit else 0,
        )

    @classmethod
    def build_quest(
        cls,
        txn_id: int,
        quest_cat_id: int,
        rub_reward: int = 0,
        quest_id: str = "",
        complete: bool = False,
    ) -> "TxnBlock":
        qbytes = quest_id.encode("ascii", errors="ignore")[:TXN_RESERVED_LEN]
        return cls(
            state=TXN_STATE_IDLE,
            txn_id=txn_id,
            amount_rub=rub_reward,
            item_id=quest_cat_id,
            op_type=TXN_OP_QUEST,
            flags=TXN_FLAG_QUEST_COMPLETE if complete else 0,
            reserved=qbytes.ljust(TXN_RESERVED_LEN, b"\x00"),
        )

    @classmethod
    def build_admit(cls, txn_id: int) -> "TxnBlock":
        return cls(state=TXN_STATE_IDLE, txn_id=txn_id, op_type=TXN_OP_ADMIT)

    def quest_id_str(self) -> str:
        return self.reserved.split(b"\x00", 1)[0].decode("ascii", errors="ignore")

    def with_pending(self) -> bytes:
        return self.pack(state=TXN_STATE_PENDING)

    def is_terminal(self) -> bool:
        return self.state in (TXN_STATE_SUCCESS, TXN_STATE_FAILED)


def apply_rank_discount(base_price: int, discount_pct: int) -> int:
    return base_price * (100 - max(0, min(100, discount_pct))) // 100


def _locked_outcome(block: TxnBlock, player_money: int) -> TxnBlock:
    out = TxnBlock(
        state=TXN_STATE_FAILED,
        txn_id=block.txn_id,
        amount_rub=block.amount_rub,
        item_id=block.item_id,
        op_type=block.op_type,
        flags=block.flags,
        result_code=TXN_RESULT_SYSTEM_LOCKED,
        balance_after=player_money,
        amount_paid=0,
        reserved=block.reserved,
    )
    return out


def pda_process_purchase(
    block: TxnBlock,
    player_money: int,
    player_level: int,
    lvl_store: int = 2,
    discount_pct: int = 0,
    system_locked: bool = False,
    player_dead: bool = False,
) -> TxnBlock:
    """Simulator-side mirror of PDA purchase handler."""
    if system_locked or player_dead:
        return _locked_outcome(block, player_money)
    if player_level < lvl_store:
        out = _locked_outcome(block, player_money)
        out.result_code = TXN_RESULT_LEVEL_TOO_LOW
        return out
    paid = apply_rank_discount(block.amount_rub, discount_pct)
    if player_money < paid:
        out = _locked_outcome(block, player_money)
        out.result_code = TXN_RESULT_INSUFFICIENT_FUNDS
        return out
    out = TxnBlock(
        state=TXN_STATE_SUCCESS,
        txn_id=block.txn_id,
        amount_rub=block.amount_rub,
        item_id=block.item_id,
        op_type=block.op_type,
        flags=TXN_FLAG_DISCOUNT if discount_pct > 0 else 0,
        result_code=TXN_RESULT_OK,
        amount_paid=paid,
        balance_after=player_money - paid,
    )
    return out


def pda_process_bank(
    block: TxnBlock,
    player_money: int,
    player_level: int,
    lvl_atm: int = 3,
    system_locked: bool = False,
    player_dead: bool = False,
) -> TxnBlock:
    if system_locked or player_dead:
        return _locked_outcome(block, player_money)
    if player_level < lvl_atm:
        out = _locked_outcome(block, player_money)
        out.result_code = TXN_RESULT_LEVEL_TOO_LOW
        return out
    deposit = bool(block.flags & TXN_FLAG_BANK_DEPOSIT)
    amt = block.amount_rub
    if amt <= 0:
        out = _locked_outcome(block, player_money)
        out.result_code = TXN_RESULT_NOT_IMPLEMENTED
        return out
    if deposit:
        if player_money < amt:
            out = _locked_outcome(block, player_money)
            out.result_code = TXN_RESULT_INSUFFICIENT_FUNDS
            return out
        return TxnBlock(
            state=TXN_STATE_SUCCESS,
            txn_id=block.txn_id,
            amount_rub=block.amount_rub,
            item_id=block.item_id,
            op_type=block.op_type,
            flags=block.flags,
            result_code=TXN_RESULT_OK,
            amount_paid=amt,
            balance_after=player_money - amt,
        )
    return TxnBlock(
        state=TXN_STATE_SUCCESS,
        txn_id=block.txn_id,
        amount_rub=block.amount_rub,
        item_id=block.item_id,
        op_type=block.op_type,
        flags=block.flags,
        result_code=TXN_RESULT_OK,
        amount_paid=amt,
        balance_after=player_money + amt,
    )


def pda_dispatch_txn(
    block: TxnBlock,
    player_money: int,
    player_level: int,
    discount_pct: int = 0,
    system_locked: bool = False,
    player_dead: bool = False,
    active_quest_ids: Optional[list[str]] = None,
) -> TxnBlock:
    """Route by op_type — mirror of PDA pollEepromTransaction dispatch."""
    if block.op_type == TXN_OP_PURCHASE:
        return pda_process_purchase(
            block, player_money, player_level,
            discount_pct=discount_pct,
            system_locked=system_locked,
            player_dead=player_dead,
        )
    if block.op_type in (TXN_OP_BANK, TXN_OP_ATM):
        return pda_process_bank(
            block, player_money, player_level,
            system_locked=system_locked,
            player_dead=player_dead,
        )
    if block.op_type == TXN_OP_QUEST:
        if system_locked or player_dead:
            return _locked_outcome(block, player_money)
        qid = block.quest_id_str() or f"q{block.item_id}"
        active = list(active_quest_ids or [])
        if block.flags & TXN_FLAG_QUEST_COMPLETE:
            if qid not in active:
                out = _locked_outcome(block, player_money)
                out.result_code = TXN_RESULT_QUEST_NOT_FOUND
                return out
            reward = max(0, block.amount_rub)
            return TxnBlock(
                state=TXN_STATE_SUCCESS,
                txn_id=block.txn_id,
                amount_rub=block.amount_rub,
                item_id=block.item_id,
                op_type=block.op_type,
                flags=block.flags,
                result_code=TXN_RESULT_OK,
                amount_paid=reward,
                balance_after=player_money + reward,
                reserved=block.reserved,
            )
        if qid in active:
            out = _locked_outcome(block, player_money)
            out.result_code = TXN_RESULT_QUEST_DUPLICATE
            return out
        return TxnBlock(
            state=TXN_STATE_SUCCESS,
            txn_id=block.txn_id,
            amount_rub=block.amount_rub,
            item_id=block.item_id,
            op_type=block.op_type,
            flags=block.flags,
            result_code=TXN_RESULT_OK,
            balance_after=player_money,
            reserved=block.reserved,
        )
    if block.op_type == TXN_OP_ADMIT:
        if system_locked:
            return TxnBlock(
                state=TXN_STATE_SUCCESS,
                txn_id=block.txn_id,
                op_type=block.op_type,
                result_code=TXN_RESULT_OK,
                balance_after=player_money,
            )
        return TxnBlock(
            state=TXN_STATE_SUCCESS,
            txn_id=block.txn_id,
            op_type=block.op_type,
            result_code=TXN_RESULT_OK,
            balance_after=player_money,
        )
    out = _locked_outcome(block, player_money)
    out.result_code = TXN_RESULT_NOT_IMPLEMENTED
    return out
