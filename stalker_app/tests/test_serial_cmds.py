"""Команды USB к ПДА — без железа."""

import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "programmat_pc"))

from serial_link import (
    build_broadcast_cmd,
    build_emission_cmd,
    build_radio_cmd,
    build_register_cmd,
    DEVICE_TYPE_MAP,
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

    def test_broadcast_and_emission(self):
        self.assertEqual(build_broadcast_cmd("Всем в ЗЗ"), "CONFIG:BROADCAST:Всем в ЗЗ")
        self.assertEqual(build_emission_cmd(60, 30),
                         "CONFIG:EMISSION:timer=60,duration=30")
        self.assertEqual(build_radio_cmd(3, 20), "CONFIG:RADIO:track=3,vol=20")

    def test_device_map_has_pda(self):
        self.assertEqual(DEVICE_TYPE_MAP["PDA"], 3)
        self.assertEqual(DEVICE_TYPE_MAP["CHIP_BOX"], 0)


if __name__ == "__main__":
    unittest.main()
