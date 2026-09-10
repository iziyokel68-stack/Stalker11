"""Доска заданий: режимы выдачи, каталог EEPROM, прошивка терминала QUEST."""

import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "programmat_pc"))

from quest_catalog import (  # noqa: E402
    QUEST_CLAIM_DONE,
    QUEST_CLAIM_FREE,
    QUEST_CLAIM_GONE,
    QUEST_CLAIM_HELD,
    QuestCard,
    next_claim_status,
    pack_catalog,
    quest_is_listed,
    unpack_catalog,
)


class QuestBoardRulesTests(unittest.TestCase):
    def test_timeout_returns_after_two_hours(self):
        self.assertFalse(quest_is_listed("timeout", QUEST_CLAIM_HELD, 119, 120))
        self.assertTrue(quest_is_listed("timeout", QUEST_CLAIM_HELD, 120, 120))
        self.assertEqual(
            next_claim_status("timeout", QUEST_CLAIM_HELD, 120, 120),
            QUEST_CLAIM_FREE,
        )

    def test_custom_timeout(self):
        self.assertFalse(quest_is_listed("timeout", QUEST_CLAIM_HELD, 29, 30))
        self.assertTrue(quest_is_listed("timeout", QUEST_CLAIM_HELD, 30, 30))

    def test_oneshot_never_returns(self):
        self.assertFalse(quest_is_listed("oneshot", QUEST_CLAIM_HELD, 999, 120))
        self.assertEqual(
            next_claim_status("oneshot", QUEST_CLAIM_HELD, 120, 120),
            QUEST_CLAIM_GONE,
        )
        self.assertFalse(quest_is_listed("oneshot", QUEST_CLAIM_GONE, 0, 120))

    def test_shared_always_listed(self):
        self.assertTrue(quest_is_listed("shared", QUEST_CLAIM_HELD, 0, 120))
        self.assertTrue(quest_is_listed("shared", QUEST_CLAIM_DONE, 0, 120))
        self.assertEqual(
            next_claim_status("shared", QUEST_CLAIM_HELD, 0, 120, completed=True),
            QUEST_CLAIM_FREE,
        )

    def test_completed_exclusive_gone(self):
        self.assertEqual(
            next_claim_status("timeout", QUEST_CLAIM_HELD, 10, 120, completed=True),
            QUEST_CLAIM_DONE,
        )
        self.assertFalse(quest_is_listed("timeout", QUEST_CLAIM_DONE, 0, 120))

    def test_pack_roundtrip(self):
        cards = [
            QuestCard("Q01", "Найти ПДА", 300, False, "timeout", 120),
            QuestCard("HIDE", "Тень", 500, True, "oneshot", 0),
            QuestCard("ALL", "Патруль", 100, False, "shared", 60),
        ]
        blob = pack_catalog(cards)
        back = unpack_catalog(blob)
        self.assertEqual(len(back), 3)
        self.assertEqual(back[0].code, "Q01")
        self.assertEqual(back[0].title, "Найти ПДА")
        self.assertEqual(back[0].rub, 300)
        self.assertEqual(back[1].hidden, True)
        self.assertEqual(back[1].mode, "oneshot")
        self.assertEqual(back[1].timeout_min, 120)
        self.assertEqual(back[2].mode, "shared")
        self.assertEqual(back[2].timeout_min, 60)

    def test_format_quest_add_title_last(self):
        from quest_catalog import format_quest_add
        cmd = format_quest_add(QuestCard("Q01", "Найти, срочно", 300, False, "timeout", 120))
        self.assertTrue(cmd.startswith("QUEST_ADD:"))
        self.assertIn("id=Q01", cmd)
        self.assertIn("mode=timeout", cmd)
        self.assertTrue(cmd.endswith("title=Найти, срочно"))

    def test_serial_flash_helper_exists(self):
        from serial_link import SerialLink
        self.assertTrue(hasattr(SerialLink, "flash_quest_catalog"))


if __name__ == "__main__":
    unittest.main()
