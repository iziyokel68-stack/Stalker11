"""Каналы TCA и правила applyChip — без железа."""

import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "modules"))

from chip_protocol import (
    CHIP_TYPE_ADMIN,
    CHIP_TYPE_ARMOR,
    CHIP_TYPE_ARTIFACT,
    CHIP_TYPE_CONSUMABLE,
    EQUIPMENT_CHANNELS,
    LVL_ARMOR,
    LVL_ARTIFACT1,
    LVL_ARTIFACT2,
    LVL_ARTIFACT3,
    MUX_CH_RESERVED_1,
    MUX_CH_RESERVED_4,
    MUX_CH_SLOT_1,
    MUX_CH_SLOT_2,
    MUX_CH_SLOT_3,
    MUX_CH_SLOT_4,
    MUX_CH_UNIVERSAL,
    ChipHeader,
    apply_antirad,
    apply_heal,
    artifact_level_required,
    can_use_on_channel,
    parse_chip,
)


class ChannelMapTests(unittest.TestCase):
    def test_cable_and_consumable_are_ch0(self):
        self.assertEqual(MUX_CH_UNIVERSAL, 0)

    def test_equipment_slots_are_2_3_5_6(self):
        self.assertEqual(EQUIPMENT_CHANNELS, (2, 3, 5, 6))
        self.assertEqual(
            (MUX_CH_SLOT_1, MUX_CH_SLOT_2, MUX_CH_SLOT_3, MUX_CH_SLOT_4),
            (2, 3, 5, 6),
        )

    def test_reserved_channels(self):
        self.assertEqual(MUX_CH_RESERVED_1, 1)
        self.assertEqual(MUX_CH_RESERVED_4, 4)


class ChipHeaderTests(unittest.TestCase):
    def test_pack_parse_roundtrip(self):
        src = ChipHeader(type=1, sub=0, uses=255, params=[10, 0, 0, 0, 0, 0, 0, 0, 5, 30, 100] + [0] * 5)
        buf = src.pack()
        got = parse_chip(buf)
        self.assertIsNotNone(got)
        self.assertEqual(got.type, 1)
        self.assertEqual(got.uses, 255)
        self.assertEqual(got.params[8], 5)
        self.assertEqual(got.params[10], 100)

    def test_bad_crc_rejected(self):
        buf = bytearray(ChipHeader(type=0, sub=0, uses=1, params=[50] + [0] * 15).pack())
        buf[0x24] ^= 0xFF
        self.assertIsNone(parse_chip(bytes(buf)))


class LevelGateTests(unittest.TestCase):
    def test_consumable_on_ch0(self):
        ok, _ = can_use_on_channel(CHIP_TYPE_CONSUMABLE, MUX_CH_UNIVERSAL, 1, 0, 0)
        self.assertTrue(ok)

    def test_armor_rejected_on_ch0(self):
        ok, msg = can_use_on_channel(CHIP_TYPE_ARMOR, MUX_CH_UNIVERSAL, 10, 0, 0)
        self.assertFalse(ok)
        self.assertIn("СЛОТ", msg)

    def test_armor_needs_level_5(self):
        ok, _ = can_use_on_channel(CHIP_TYPE_ARMOR, MUX_CH_SLOT_1, 4, 0, 0)
        self.assertFalse(ok)
        ok, _ = can_use_on_channel(CHIP_TYPE_ARMOR, MUX_CH_SLOT_1, LVL_ARMOR, 0, 0)
        self.assertTrue(ok)

    def test_one_armor_only(self):
        ok, msg = can_use_on_channel(CHIP_TYPE_ARMOR, MUX_CH_SLOT_3, 10, 1, 0)
        self.assertFalse(ok)
        self.assertIn("БРОНЯ", msg)

    def test_artifacts_by_count_not_channel(self):
        self.assertEqual(artifact_level_required(0), LVL_ARTIFACT1)
        self.assertEqual(artifact_level_required(1), LVL_ARTIFACT2)
        self.assertEqual(artifact_level_required(2), LVL_ARTIFACT3)
        ok, _ = can_use_on_channel(CHIP_TYPE_ARTIFACT, MUX_CH_SLOT_4, 2, 0, 0)
        self.assertTrue(ok)
        ok, _ = can_use_on_channel(CHIP_TYPE_ARTIFACT, MUX_CH_SLOT_2, 6, 0, 1)
        self.assertFalse(ok)
        ok, _ = can_use_on_channel(CHIP_TYPE_ARTIFACT, MUX_CH_SLOT_2, 7, 0, 1)
        self.assertTrue(ok)
        ok, _ = can_use_on_channel(CHIP_TYPE_ARTIFACT, MUX_CH_SLOT_1, 10, 0, 3)
        self.assertFalse(ok)

    def test_consumable_not_in_equipment(self):
        ok, msg = can_use_on_channel(CHIP_TYPE_CONSUMABLE, MUX_CH_SLOT_1, 10, 0, 0)
        self.assertFalse(ok)
        self.assertIn("CH0", msg)

    def test_admin_on_ch0_when_dead(self):
        ok, _ = can_use_on_channel(
            CHIP_TYPE_ADMIN, MUX_CH_UNIVERSAL, 1, 0, 0, is_dead=True
        )
        self.assertTrue(ok)

    def test_consumable_blocked_when_locked(self):
        ok, _ = can_use_on_channel(
            CHIP_TYPE_CONSUMABLE, MUX_CH_UNIVERSAL, 10, 0, 0, admit_pending=True
        )
        self.assertFalse(ok)


class ApplyEffectTests(unittest.TestCase):
    def test_heal_flat_and_pct(self):
        self.assertEqual(apply_heal(100, 1000, 50, False), 150)
        self.assertEqual(apply_heal(100, 1000, 10, True), 200)
        self.assertEqual(apply_heal(990, 1000, 50, False), 1000)

    def test_antirad(self):
        self.assertEqual(apply_antirad(80, 30, False, 1000), 50)
        self.assertEqual(apply_antirad(500, 10, True, 1000), 400)


if __name__ == "__main__":
    unittest.main()
