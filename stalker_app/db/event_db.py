"""
STALKER App — общая база игроков события (SQLite)
====================================================
Схема согласована в docs/PROGRESSION.txt §10.2.

Файл на событие: stalker_event_<event_id>.db — переносимый, USB export/import.

Таблицы:
  events        — одно событие (игровой день/сезон)
  players       — игроки события, привязка к PDA (pda_uid) опциональна
  player_stats  — снимок прогресса игрока в конце игры/смены (модуль "Сбор статистики")

Модуль не тянет pygame/tkinter — чистый доступ к данным, чтобы им могли
пользоваться и модуль регистрации, и модуль статистики, и позже сам
programmer.py при интеграции.
"""

import csv
import os
import sqlite3
import time
import uuid
from dataclasses import dataclass, field
from typing import Optional


SCHEMA = """
CREATE TABLE IF NOT EXISTS events (
    event_id    TEXT PRIMARY KEY,
    title       TEXT NOT NULL,
    created_at  REAL NOT NULL,
    closed_at   REAL
);

CREATE TABLE IF NOT EXISTS players (
    player_id     INTEGER PRIMARY KEY AUTOINCREMENT,
    event_id      TEXT NOT NULL REFERENCES events(event_id),
    name          TEXT NOT NULL,
    callsign      TEXT,
    group_name    TEXT,
    pda_uid       TEXT,
    registered_at REAL,
    registered_by TEXT,
    notes         TEXT
);

CREATE UNIQUE INDEX IF NOT EXISTS ux_players_pda_uid
    ON players(event_id, pda_uid) WHERE pda_uid IS NOT NULL;

CREATE TABLE IF NOT EXISTS player_stats (
    stat_id             INTEGER PRIMARY KEY AUTOINCREMENT,
    player_id           INTEGER NOT NULL REFERENCES players(player_id),
    event_id            TEXT NOT NULL REFERENCES events(event_id),
    collected_at        REAL NOT NULL,
    level               INTEGER,
    xp                  INTEGER,
    money_rub           INTEGER,
    deaths              INTEGER,
    cheat_shield_count  INTEGER,
    achievements_json   TEXT,
    rank_title          TEXT,
    pda_snapshot_json   TEXT
);
"""


def default_db_path(event_id: str, base_dir: Optional[str] = None) -> str:
    base_dir = base_dir or os.getcwd()
    return os.path.join(base_dir, f"stalker_event_{event_id}.db")


def new_event_id() -> str:
    return time.strftime("%Y%m%d_%H%M%S") + "_" + uuid.uuid4().hex[:6]


@dataclass
class Player:
    player_id: int
    event_id: str
    name: str
    callsign: Optional[str]
    group_name: Optional[str]
    pda_uid: Optional[str]
    registered_at: Optional[float]
    registered_by: Optional[str]
    notes: Optional[str]


@dataclass
class PlayerStat:
    stat_id: int
    player_id: int
    event_id: str
    collected_at: float
    level: Optional[int]
    xp: Optional[int]
    money_rub: Optional[int]
    deaths: Optional[int]
    cheat_shield_count: Optional[int]
    achievements_json: Optional[str]
    rank_title: Optional[str]
    pda_snapshot_json: Optional[str]


class EventDB:
    """Обёртка над SQLite-файлом одного события."""

    def __init__(self, path: str):
        self.path = path
        self.conn = sqlite3.connect(path)
        self.conn.row_factory = sqlite3.Row
        self.conn.executescript(SCHEMA)
        self.conn.commit()

    def close(self):
        self.conn.close()

    # --- events ---------------------------------------------------------

    def create_event(self, title: str, event_id: Optional[str] = None) -> str:
        event_id = event_id or new_event_id()
        self.conn.execute(
            "INSERT OR IGNORE INTO events(event_id, title, created_at, closed_at) "
            "VALUES (?, ?, ?, NULL)",
            (event_id, title, time.time()),
        )
        self.conn.commit()
        return event_id

    def get_event(self, event_id: str):
        return self.conn.execute(
            "SELECT * FROM events WHERE event_id=?", (event_id,)
        ).fetchone()

    def list_events(self):
        return self.conn.execute(
            "SELECT * FROM events ORDER BY created_at DESC"
        ).fetchall()

    def close_event(self, event_id: str):
        self.conn.execute(
            "UPDATE events SET closed_at=? WHERE event_id=?",
            (time.time(), event_id),
        )
        self.conn.commit()

    # --- players ---------------------------------------------------------

    def add_player(self, event_id: str, name: str, callsign: str = "",
                    group_name: str = "", notes: str = "") -> int:
        cur = self.conn.execute(
            "INSERT INTO players(event_id, name, callsign, group_name, "
            "pda_uid, registered_at, registered_by, notes) "
            "VALUES (?, ?, ?, ?, NULL, NULL, NULL, ?)",
            (event_id, name.strip(), callsign.strip(), group_name.strip(), notes.strip()),
        )
        self.conn.commit()
        return cur.lastrowid

    def update_player(self, player_id: int, **fields):
        if not fields:
            return
        allowed = {"name", "callsign", "group_name", "notes", "pda_uid",
                   "registered_at", "registered_by"}
        cols = [k for k in fields if k in allowed]
        if not cols:
            return
        set_clause = ", ".join(f"{c}=?" for c in cols)
        values = [fields[c] for c in cols] + [player_id]
        self.conn.execute(
            f"UPDATE players SET {set_clause} WHERE player_id=?", values
        )
        self.conn.commit()

    def bind_pda(self, player_id: int, pda_uid: str, registered_by: str = "") -> None:
        """Привязать PDA UID к игроку (CONFIG:REGISTER на железе)."""
        self.update_player(
            player_id,
            pda_uid=pda_uid.strip(),
            registered_at=time.time(),
            registered_by=registered_by.strip(),
        )

    def delete_player(self, player_id: int):
        self.conn.execute("DELETE FROM players WHERE player_id=?", (player_id,))
        self.conn.execute("DELETE FROM player_stats WHERE player_id=?", (player_id,))
        self.conn.commit()

    def list_players(self, event_id: str):
        rows = self.conn.execute(
            "SELECT * FROM players WHERE event_id=? ORDER BY player_id",
            (event_id,),
        ).fetchall()
        return [Player(**{k: r[k] for k in r.keys()}) for r in rows]

    def get_player(self, player_id: int):
        r = self.conn.execute(
            "SELECT * FROM players WHERE player_id=?", (player_id,)
        ).fetchone()
        return Player(**{k: r[k] for k in r.keys()}) if r else None

    def find_by_pda_uid(self, event_id: str, pda_uid: str):
        r = self.conn.execute(
            "SELECT * FROM players WHERE event_id=? AND pda_uid=?",
            (event_id, pda_uid),
        ).fetchone()
        return Player(**{k: r[k] for k in r.keys()}) if r else None

    # --- player_stats ------------------------------------------------------

    def record_stat(self, player_id: int, event_id: str, *, level=None, xp=None,
                     money_rub=None, deaths=None, cheat_shield_count=None,
                     achievements_json=None, rank_title=None,
                     pda_snapshot_json=None) -> int:
        cur = self.conn.execute(
            "INSERT INTO player_stats(player_id, event_id, collected_at, level, "
            "xp, money_rub, deaths, cheat_shield_count, achievements_json, "
            "rank_title, pda_snapshot_json) VALUES (?,?,?,?,?,?,?,?,?,?,?)",
            (player_id, event_id, time.time(), level, xp, money_rub, deaths,
             cheat_shield_count, achievements_json, rank_title, pda_snapshot_json),
        )
        self.conn.commit()
        return cur.lastrowid

    def latest_stat(self, player_id: int):
        r = self.conn.execute(
            "SELECT * FROM player_stats WHERE player_id=? "
            "ORDER BY collected_at DESC LIMIT 1",
            (player_id,),
        ).fetchone()
        return PlayerStat(**{k: r[k] for k in r.keys()}) if r else None

    def list_stats(self, event_id: str):
        rows = self.conn.execute(
            "SELECT * FROM player_stats WHERE event_id=? ORDER BY collected_at",
            (event_id,),
        ).fetchall()
        return [PlayerStat(**{k: r[k] for k in r.keys()}) for r in rows]

    # --- export -----------------------------------------------------------

    def export_players_csv(self, event_id: str, csv_path: str):
        players = self.list_players(event_id)
        with open(csv_path, "w", newline="", encoding="utf-8-sig") as f:
            w = csv.writer(f)
            w.writerow(["player_id", "name", "callsign", "group_name",
                        "pda_uid", "registered_at", "registered_by", "notes"])
            for p in players:
                w.writerow([p.player_id, p.name, p.callsign or "", p.group_name or "",
                            p.pda_uid or "", p.registered_at or "",
                            p.registered_by or "", p.notes or ""])

    def export_stats_csv(self, event_id: str, csv_path: str):
        players_by_id = {p.player_id: p for p in self.list_players(event_id)}
        stats = self.list_stats(event_id)
        with open(csv_path, "w", newline="", encoding="utf-8-sig") as f:
            w = csv.writer(f)
            w.writerow(["player_id", "name", "callsign", "collected_at",
                        "level", "xp", "money_rub", "deaths",
                        "cheat_shield_count", "rank_title"])
            for s in stats:
                p = players_by_id.get(s.player_id)
                w.writerow([s.player_id, p.name if p else "", p.callsign if p else "",
                            time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(s.collected_at)),
                            s.level, s.xp, s.money_rub, s.deaths,
                            s.cheat_shield_count, s.rank_title or ""])
