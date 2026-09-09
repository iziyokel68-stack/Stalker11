"""Команды USB / LoRa / EEPROM — без железа."""

import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "programmat_pc"))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from serial_link import (
    DEVICE_TYPE_MAP,
    build_broadcast_cmd,
    build_emission_cmd,
    build_lora_cmd,
    build_radio_cmd,
    build_register_chip,
    build_register_cmd,
    build_volume_cmd,
    emission_seconds,
    parse_device_id,
)
from config_builder import build_chip_config, build_config_for_device
from shared.master_channel import (
    id_choices,
    lora_emission,
    lora_radio,
    player_id_from_target,
)


class SerialCmdTests(unittest.TestCase):
    def test_register_cmd(self):
        cmd = build_register_cmd("Иван", "Волк", "Долг")
        self.assertTrue(cmd.startswith("CONFIG:REGISTER:"))
        self.assertIn("name=Иван", cmd)
        self.assertIn("callsign=Волк", cmd)
        self.assertIn("group=Долг", cmd)

    def test_register_strips_comma(self):
        cmd = build_register_cmd("Иван, Петр")
        self.assertNotIn("name=Иван,", cmd)
        self.assertIn("name=Иван  Петр", cmd)

    def test_broadcast_emission_minutes(self):
        self.assertEqual(build_broadcast_cmd("Всем в ЗЗ"), "CONFIG:BROADCAST:Всем в ЗЗ")
        self.assertEqual(
            build_emission_cmd(30, 5),
            "CONFIG:EMISSION:timer_min=30,duration_min=5",
        )
        self.assertEqual(emission_seconds(30, 5), (1800, 300))
        self.assertEqual(build_radio_cmd(3, 20), "CONFIG:RADIO:track=3")
        self.assertEqual(build_volume_cmd(40), "CONFIG:VOLUME:level=30")

    def test_lora_and_register_chip(self):
        cmd = build_lora_cmd(4, "EMISSION", 1800, 300)
        self.assertEqual(cmd, "LORA_TX:to=4,msg=EMISSION,v1=1800,v2=300")
        chip = build_register_chip(4, "Иван")
        self.assertIn("type=3", chip)
        self.assertIn("sub=7", chip)
        self.assertIn("p0=4", chip)
        self.assertIn("name=Иван", chip)

    def test_player_target_and_lora_helpers(self):
        self.assertEqual(player_id_from_target("all"), 0)
        self.assertEqual(player_id_from_target("0"), 0)
        self.assertEqual(player_id_from_target("player:12:Иван"), 12)
        self.assertEqual(player_id_from_target("12"), 12)
        self.assertEqual(player_id_from_target("group:Долг"), 0)
        self.assertIn("msg=EMISSION", lora_emission(0, 30, 5))
        self.assertIn("v1=1800", lora_emission(0, 30, 5))
        self.assertEqual(lora_radio(7, 2), "LORA_TX:to=7,msg=RADIO,v1=2,v2=0")
        class P:
            def __init__(self, i):
                self.player_id = i
        self.assertEqual(id_choices([P(3), P(9)]), ["0", "3", "9"])

    def test_parse_device_id(self):
        self.assertEqual(parse_device_id("STALKER:ANOMALY:v1,id=AABBCC"), "AABBCC")
        self.assertEqual(parse_device_id("BEACON:id=00FF12"), "00FF12")
        self.assertEqual(parse_device_id("UID:00FF12"), "00FF12")
        self.assertIsNone(parse_device_id("STALKER:CHIP_BOX:v1"))

    def test_chip_config_registration(self):
        s = build_chip_config({
            "chip_type": 3, "chip_sub": 7, "uses": 1,
            "params": [12], "reg_name": "Волк",
        })
        self.assertIn("type=3,sub=7", s)
        self.assertIn("p0=12", s)
        self.assertIn("name=Волк", s)
        pda = build_config_for_device("PDA", {"pda_mode": 0, "pda_func_flags": 255})
        self.assertTrue(pda.startswith("CONFIG:FUNC:"))

    def test_device_map_has_pda(self):
        self.assertEqual(DEVICE_TYPE_MAP["PDA"], 3)
        self.assertEqual(DEVICE_TYPE_MAP["CHIP_BOX"], 0)


if __name__ == "__main__":
    unittest.main()
