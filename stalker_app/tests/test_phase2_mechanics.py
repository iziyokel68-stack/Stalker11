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
    p.in_agony = False
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

    def test_ten_percent_hp_is_not_agony(self):
        p = _player()
        p.health = 100
        self.assertFalse(p.is_in_agony())
        p.health = 1
        self.assertFalse(p.is_in_agony())

    def test_ordinary_hp_zero_is_agony_not_dead(self):
        p = _player()
        p.health = 80
        evt = p.apply_damage(500, source_id="anom1")
        self.assertEqual(evt["type"], "agony")
        self.assertTrue(p.is_in_agony())
        self.assertFalse(p.is_dead)
        self.assertFalse(p.is_zombie)
        self.assertEqual(p.health, 0)
        self.assertEqual(p.death_counter, 0)

    def test_heal_from_agony(self):
        p = _player()
        p.health = 10
        p.apply_damage(400, source_id="anom1")
        self.assertTrue(p.is_in_agony())
        evt = p.apply_heal(200)
        self.assertEqual(evt["type"], "heal")
        self.assertFalse(p.is_in_agony())
        self.assertGreater(p.health, 0)
        self.assertFalse(p.is_dead)

    def test_medkit_from_agony(self):
        p = _player()
        p.health = 10
        p.apply_damage(400, source_id="anom1")
        chip = ItemChip(item_type=ItemChip.TYPE_MEDKIT, value=150)
        evt = p.use_consumable(chip)
        self.assertEqual(evt["type"], "heal")
        self.assertFalse(p.is_in_agony())

    def test_killing_blow_from_agony(self):
        p = _player()
        p.health = 10
        p.apply_damage(400, source_id="anom1")
        self.assertTrue(p.is_in_agony())
        evt = p.apply_damage(10, source_id="anom2")
        self.assertEqual(evt["type"], "death")
        self.assertTrue(p.is_dead)
        self.assertFalse(p.is_in_agony())
        self.assertEqual(p.death_counter, 1)

    def test_surrender_from_agony(self):
        p = _player()
        p.health = 0
        p.in_agony = True
        evt = p.surrender()
        self.assertEqual(evt["type"], "death")
        self.assertTrue(p.is_dead)
        self.assertFalse(p.is_in_agony())

    def test_psi_skips_agony_to_zombie(self):
        p = _player()
        p.health = 50
        evt = p.apply_damage(500, source_id="controller_psi")
        self.assertEqual(evt["type"], "zombie")
        self.assertTrue(p.is_zombie)
        self.assertFalse(p.is_in_agony())
        self.assertFalse(p.is_dead)

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

    def test_zombie_silent_except_admin_broadcast(self):
        p = _player()
        p.is_zombie = True
        p.notifications = []
        silent = p.add_notification("ПЕРЕВОД +100", "info")
        self.assertFalse(silent.get("notify"))
        self.assertEqual(p.notifications, [])
        p.grant_achievement("death_first")
        self.assertEqual(p.notifications, [])
        shown = p.add_notification("ВНИМАНИЕ МАСТЕРА", "broadcast", admin=True)
        self.assertTrue(shown.get("notify"))
        self.assertEqual(len(p.notifications), 1)

    def test_zombie_incoming_money_and_quest_silent(self):
        p = _player()
        p.is_zombie = True
        p.notifications = []
        p.add_money(500)
        self.assertEqual(p.money, 1500)
        chip = ItemChip(
            item_type=ItemChip.TYPE_CONSUMABLE,
            modifiers={"quest": True, "quest_id": "z1", "title": "Тихое"},
        )
        evt = p._use_quest_chip(chip)
        self.assertEqual(evt["type"], "info")
        self.assertEqual(len(p.get_active_tasks()), 1)
        self.assertEqual(p.notifications, [])
        med = ItemChip(item_type=ItemChip.TYPE_MEDKIT, value=100)
        blocked = p.use_consumable(med)
        self.assertEqual(blocked["type"], "error")
        self.assertFalse(p.spend_money(10))

    def test_zombie_sees_emission_broadcast(self):
        p = _player()
        p.is_zombie = True
        p.notifications = []
        p.start_emission(5, 20)
        self.assertEqual(len(p.notifications), 1)
        self.assertEqual(p.notifications[0]["type"], "emission")


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
