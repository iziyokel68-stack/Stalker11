"""Tests for EventDB (registration, broadcasts, quests, export)."""

import csv
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from db.event_db import EventDB, new_event_id, default_db_path


class EventDBTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.event_id = new_event_id()
        self.path = default_db_path(self.event_id, self.tmp.name)
        self.db = EventDB(self.path)
        self.db.create_event("Тестовый день", self.event_id)

    def tearDown(self):
        self.db.close()
        self.tmp.cleanup()

    def test_add_and_list_players(self):
        pid = self.db.add_player(self.event_id, "Иван", callsign="Волк",
                                 group_name="Долг")
        players = self.db.list_players(self.event_id)
        self.assertEqual(len(players), 1)
        self.assertEqual(players[0].player_id, pid)
        self.assertEqual(players[0].name, "Иван")
        self.assertEqual(players[0].status, "new")

    def test_search_and_group_filter(self):
        self.db.add_player(self.event_id, "Анна", group_name="Свобода")
        self.db.add_player(self.event_id, "Борис", callsign="Клык", group_name="Долг")
        found = self.db.list_players(self.event_id, query="клык")
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0].name, "Борис")
        duty = self.db.list_players(self.event_id, group_name="Долг")
        self.assertEqual(len(duty), 1)
        self.assertEqual(self.db.list_groups(self.event_id), ["Долг", "Свобода"])

    def test_register_and_admit(self):
        pid = self.db.add_player(self.event_id, "Иван")
        self.db.mark_registered(pid, registered_by="usb", pda_uid="AABBCCDDEEFF")
        p = self.db.get_player(pid)
        self.assertEqual(p.status, "registered")
        self.assertEqual(p.pda_uid, "AABBCCDDEEFF")
        self.db.mark_admitted(pid, admitted_by="usb")
        p = self.db.get_player(pid)
        self.assertEqual(p.status, "admitted")
        self.assertIsNotNone(p.admitted_at)
        counts = self.db.player_counts(self.event_id)
        self.assertEqual(counts["total"], 1)
        self.assertEqual(counts["admitted"], 1)
        self.assertEqual(counts["bound"], 1)

    def test_unique_pda_uid(self):
        a = self.db.add_player(self.event_id, "А")
        b = self.db.add_player(self.event_id, "Б")
        self.db.bind_pda(a, "UID1")
        with self.assertRaises(Exception):
            self.db.bind_pda(b, "UID1")

    def test_broadcasts_and_quests(self):
        bid = self.db.add_broadcast(self.event_id, "Всем в укрытие",
                                    kind="warning", target="all")
        self.db.update_broadcast(bid, status="sent", serial_response="OK")
        items = self.db.list_broadcasts(self.event_id)
        self.assertEqual(len(items), 1)
        self.assertEqual(items[0].status, "sent")
        qid = self.db.add_quest(self.event_id, "Q01", "Первое поручение",
                                body="Найти ПДА", reward_rub=300, hidden=False)
        q = self.db.get_quest(qid)
        self.assertEqual(q.title, "Первое поручение")
        self.db.update_quest(qid, reward_rub=500)
        self.assertEqual(self.db.get_quest(qid).reward_rub, 500)

    def test_close_event_and_csv(self):
        pid = self.db.add_player(self.event_id, "Иван", callsign="Волк")
        self.db.record_stat(pid, self.event_id, level=5, xp=680, money_rub=2000)
        self.db.close_event(self.event_id)
        self.assertTrue(self.db.is_closed(self.event_id))
        csv_path = os.path.join(self.tmp.name, "players.csv")
        self.db.export_players_csv(self.event_id, csv_path)
        with open(csv_path, encoding="utf-8-sig") as f:
            rows = list(csv.reader(f))
        self.assertEqual(rows[1][1], "Иван")

    def test_import_export_db_copy(self):
        self.db.add_player(self.event_id, "Иван")
        dest = os.path.join(self.tmp.name, "copy.db")
        self.db.export_db_copy(dest)
        dest2 = EventDB.import_db_copy(dest, self.tmp.name)
        other = EventDB(dest2)
        try:
            self.assertEqual(len(other.list_players(self.event_id)), 1)
        finally:
            other.close()


if __name__ == "__main__":
    unittest.main()
