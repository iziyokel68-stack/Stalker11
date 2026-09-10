"""Фаза 2: выброс, зомби, агония, античит, скрытый квест — эталон game_logic."""

import os
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "programmat_pc"))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "modules"))

from game_logic import (  # noqa: E402
    EMISSION_DMG_PER_TICK,
    ItemChip,
    Player,
    RAD_SICKNESS_TICK_DMG,
)
from chip_protocol import (  # noqa: E402
    CHIP_TYPE_QUEST,
    CHIP_TYPE_SHOP,
    LVL_HIDDEN_QUEST,
    MUX_CH_UNIVERSAL,
    can_use_on_channel,
)


def _player():
    fd, path = tempfile.mkstemp(suffix=".json")
    os.close(fd)
    p = Player(save_file=path)
    p.admit_pending = False
    p.registered = True
    p.is_dead = False
    p.is_zombie = False
    p.health = 1000
    p.max_health = 1000
    p.radiation = 0
    p.max_rad = 1000
    p.level = 12
    return p


class EmissionZombieTests(unittest.TestCase):
    def test_start_emission_sets_timers(self):
        p = _player()
        p.start_emission(5, 20)
        self.assertEqual(p.emission_timer, 5)
        self.assertEqual(p.emission_duration, 20)

    def test_emission_countdown_then_strike(self):
        p = _player()
        p.start_emission(2, 12)
        p.tick()
        self.assertEqual(p.emission_timer, 1)
        p.tick()
        self.assertEqual(p.emission_timer, 0)
        for _ in range(10):
            p.tick()
        self.assertLess(p.health, 1000)
        self.assertGreater(p.radiation, 0)

    def test_emission_death_makes_zombie(self):
        p = _player()
        p.health = 200
        p.start_emission(0, 12)
        for _ in range(12):
            p.tick()
        self.assertTrue(p.is_zombie)
        self.assertFalse(p.is_dead)

    def test_sz_without_emit_flag_does_not_shelter(self):
        p = _player()
        p.sz_emission_protect = False
        p.last_safe_zone_time = time.time()
        p.start_emission(0, 12)
        p.tick()
        p.tick()
        p.tick()
        p.tick()
        p.tick()
        p.tick()
        p.tick()
        p.tick()
        p.tick()
        p.tick()
        self.assertLess(p.health, 1000)

    def test_sz_with_emit_flag_shelters(self):
        p = _player()
        p.sz_emission_protect = True
        p.last_safe_zone_time = time.time()
        p.start_emission(0, 12)
        hp = p.health
        for _ in range(12):
            p.tick()
        self.assertEqual(p.health, hp)


class RadAgonyShieldTests(unittest.TestCase):
    def test_rad_100_percent_is_zombie(self):
        p = _player()
        p.apply_radiation(1000)
        self.assertTrue(p.is_zombie)

    def test_rad_sickness_ticks_hp(self):
        p = _player()
        p.radiation = 600
        for _ in range(59):
            p.tick()
        self.assertEqual(p.health, 1000)
        p.tick()
        self.assertEqual(p.health, 1000 - RAD_SICKNESS_TICK_DMG)
        self.assertIn("rad_sick", p.achievements_unlocked)

    def test_agony_at_10_percent(self):
        p = _player()
        p.health = 100
        self.assertTrue(p.is_in_agony())
        p.health = 101
        self.assertFalse(p.is_in_agony())

    def test_cheat_shield_on_rssi_drop(self):
        p = _player()
        p.in_anomaly_zone = True
        p.last_packet_time = time.time() - 4.0
        p.rssi_history = [-70]
        p.tick()
        self.assertEqual(p.cheat_shield_count, 1)
        self.assertFalse(p.in_anomaly_zone)
        self.assertEqual(p.health, 1000)

    def test_revive_refused_for_zombie(self):
        p = _player()
        p.is_zombie = True
        evt = p.grant_revive()
        self.assertEqual(evt["type"], "error")
        self.assertTrue(p.is_zombie)

    def test_surrender_clears_zombie(self):
        p = _player()
        p.is_zombie = True
        evt = p.surrender()
        self.assertFalse(p.is_zombie)
        self.assertTrue(p.is_dead)
        self.assertEqual(evt["type"], "death")


class HiddenQuestAndChipsTests(unittest.TestCase):
    def test_hidden_quest_blocked_below_20(self):
        p = _player()
        p.level = 19
        chip = ItemChip(item_type=ItemChip.TYPE_CONSUMABLE, modifiers={"quest": True, "hidden": True, "quest_id": "h1"})
        evt = p.use_consumable(chip)
        self.assertEqual(evt["type"], "error")
        self.assertIn("20", evt["text"])

    def test_hidden_quest_ok_at_20(self):
        p = _player()
        p.level = LVL_HIDDEN_QUEST
        chip = ItemChip(
            item_type=ItemChip.TYPE_CONSUMABLE,
            modifiers={"quest": True, "hidden": True, "quest_id": "h1", "title": "Тень"},
        )
        evt = p.use_consumable(chip)
        self.assertEqual(evt["type"], "info")
        self.assertEqual(len(p.get_active_tasks()), 1)

    def test_quest_and_shop_chips_on_ch0(self):
        ok, _ = can_use_on_channel(CHIP_TYPE_QUEST, MUX_CH_UNIVERSAL, 20, 0, 0)
        self.assertTrue(ok)
        ok, _ = can_use_on_channel(CHIP_TYPE_SHOP, MUX_CH_UNIVERSAL, 2, 0, 0)
        self.assertTrue(ok)
        ok, msg = can_use_on_channel(CHIP_TYPE_SHOP, MUX_CH_UNIVERSAL, 1, 0, 0)
        self.assertFalse(ok)

    def test_emission_tick_damage_constant(self):
        self.assertEqual(EMISSION_DMG_PER_TICK, 250)


if __name__ == "__main__":
    unittest.main()
