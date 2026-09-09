"""Unit tests for EEPROM cashier↔PDA transaction protocol."""

from eeprom_txn import (
    TxnBlock,
    TXN_STATE_PENDING,
    TXN_STATE_SUCCESS,
    TXN_STATE_FAILED,
    TXN_RESULT_OK,
    TXN_RESULT_INSUFFICIENT_FUNDS,
    TXN_RESULT_LEVEL_TOO_LOW,
    TXN_RESULT_SYSTEM_LOCKED,
    TXN_RESULT_QUEST_DUPLICATE,
    TXN_FLAG_BANK_DEPOSIT,
    TXN_FLAG_QUEST_COMPLETE,
    pda_process_purchase,
    pda_process_bank,
    pda_dispatch_txn,
    apply_rank_discount,
)


def test_pack_unpack_roundtrip():
    blk = TxnBlock.build_purchase(txn_id=42, amount_rub=500, item_id=7)
    raw = blk.with_pending()
    parsed = TxnBlock.unpack(raw)
    assert parsed.state == TXN_STATE_PENDING
    assert parsed.txn_id == 42
    assert parsed.amount_rub == 500
    assert parsed.item_id == 7


def test_pda_success():
    req = TxnBlock.build_purchase(1, 500, item_id=3)
    res = pda_process_purchase(
        TxnBlock.unpack(req.with_pending()),
        player_money=1000,
        player_level=5,
        discount_pct=10,
    )
    assert res.state == TXN_STATE_SUCCESS
    assert res.result_code == TXN_RESULT_OK
    assert res.amount_paid == 450
    assert res.balance_after == 550


def test_pda_insufficient():
    req = TxnBlock.build_purchase(2, 500, item_id=1)
    res = pda_process_purchase(
        TxnBlock.unpack(req.with_pending()),
        player_money=100,
        player_level=5,
    )
    assert res.state == TXN_STATE_FAILED
    assert res.result_code == TXN_RESULT_INSUFFICIENT_FUNDS


def test_pda_level_gate():
    req = TxnBlock.build_purchase(3, 100, item_id=1)
    res = pda_process_purchase(
        TxnBlock.unpack(req.with_pending()),
        player_money=1000,
        player_level=1,
    )
    assert res.state == TXN_STATE_FAILED
    assert res.result_code == TXN_RESULT_LEVEL_TOO_LOW


def test_pda_locked():
    req = TxnBlock.build_purchase(4, 100, item_id=1)
    res = pda_process_purchase(
        TxnBlock.unpack(req.with_pending()),
        player_money=1000,
        player_level=5,
        system_locked=True,
    )
    assert res.state == TXN_STATE_FAILED
    assert res.result_code == TXN_RESULT_SYSTEM_LOCKED


def test_discount():
    assert apply_rank_discount(500, 18) == 410


def test_bank_deposit():
    req = TxnBlock.build_bank(10, 300, deposit=True)
    res = pda_process_bank(
        TxnBlock.unpack(req.with_pending()),
        player_money=1000,
        player_level=5,
    )
    assert res.state == TXN_STATE_SUCCESS
    assert res.amount_paid == 300
    assert res.balance_after == 700


def test_bank_withdraw():
    req = TxnBlock.build_bank(11, 200, deposit=False)
    res = pda_process_bank(
        TxnBlock.unpack(req.with_pending()),
        player_money=500,
        player_level=5,
    )
    assert res.state == TXN_STATE_SUCCESS
    assert res.balance_after == 700


def test_quest_accept_dispatch():
    req = TxnBlock.build_quest(20, quest_cat_id=3, quest_id="qtest")
    res = pda_dispatch_txn(
        TxnBlock.unpack(req.with_pending()),
        player_money=1000,
        player_level=5,
        active_quest_ids=[],
    )
    assert res.state == TXN_STATE_SUCCESS


def test_quest_duplicate():
    req = TxnBlock.build_quest(21, quest_cat_id=3, quest_id="qtest")
    res = pda_dispatch_txn(
        TxnBlock.unpack(req.with_pending()),
        player_money=1000,
        player_level=5,
        active_quest_ids=["qtest"],
    )
    assert res.state == TXN_STATE_FAILED
    assert res.result_code == TXN_RESULT_QUEST_DUPLICATE


if __name__ == "__main__":
    test_pack_unpack_roundtrip()
    test_pda_success()
    test_pda_insufficient()
    test_pda_level_gate()
    test_pda_locked()
    test_discount()
    test_bank_deposit()
    test_bank_withdraw()
    test_quest_accept_dispatch()
    test_quest_duplicate()
    print("All eeprom_txn tests passed.")
