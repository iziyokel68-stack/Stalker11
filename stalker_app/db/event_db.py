"""
STALKER App — общая база игроков события (SQLite)
====================================================
Схема: docs/PROGRESSION.txt §10.2 + расширения Фазы 4
(допуск, оповещения, каталог квестов, USB import/export).

Файл на событие: stalker_event_<event_id>.db — переносимый, USB export/import.
"""

import csv
import os
import shutil
import sqlite3
import time
import uuid
from dataclasses import dataclass
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
    notes         TEXT,
    admitted_at   REAL,
    admitted_by   TEXT,
    status        TEXT DEFAULT 'new'
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

CREATE TABLE IF NOT EXISTS broadcasts (
    broadcast_id     INTEGER PRIMARY KEY AUTOINCREMENT,
    event_id         TEXT NOT NULL REFERENCES events(event_id),
    created_at       REAL NOT NULL,
    author           TEXT,
    kind             TEXT NOT NULL,
    target           TEXT NOT NULL,
    text             TEXT NOT NULL,
    status           TEXT NOT NULL,
    serial_response  TEXT
);

CREATE TABLE IF NOT EXISTS quests (
    quest_id     INTEGER PRIMARY KEY AUTOINCREMENT,
    event_id     TEXT NOT NULL REFERENCES events(event_id),
    code         TEXT NOT NULL,
    title        TEXT NOT NULL,
    body         TEXT,
    reward_rub   INTEGER NOT NULL DEFAULT 0,
    hidden       INTEGER NOT NULL DEFAULT 0,
    created_at   REAL NOT NULL
);

CREATE TABLE IF NOT EXISTS map_meta (
    event_id    TEXT PRIMARY KEY REFERENCES events(event_id),
    image_path  TEXT,
    updated_at  REAL NOT NULL
);

CREATE TABLE IF NOT EXISTS map_beacons (
    beacon_id   INTEGER PRIMARY KEY AUTOINCREMENT,
    event_id    TEXT NOT NULL REFERENCES events(event_id),
    name        TEXT NOT NULL,
    kind        TEXT NOT NULL,
    x_pct       REAL NOT NULL,
    y_pct       REAL NOT NULL,
    note        TEXT
);
"""

PLAYER_STATUSES = ("new", "registered", "admitted")
BROADCAST_KINDS = ("info", "warning", "emission", "radio")
BROADCAST_STATUSES = ("queued", "sent", "failed")
BEACON_KINDS = ("uwb", "shelter", "anomaly", "checkpoint", "other")


def default_db_path(event_id: str, base_dir: Optional[str] = None) -> str:
    base_dir = base_dir or os.getcwd()
    return os.path.join(base_dir, f"stalker_event_{event_id}.db")


def new_event_id() -> str:
    return time.strftime("%Y%m%d_%H%M%S") + "_" + uuid.uuid4().hex[:6]


def list_event_files(base_dir: str):
    """Список .db в каталоге событий (новые сверху по имени файла)."""
    if not os.path.isdir(base_dir):
        return []
    files = [f for f in os.listdir(base_dir) if f.endswith(".db")]
    files.sort(reverse=True)
    return [os.path.join(base_dir, f) for f in files]


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
    admitted_at: Optional[float] = None
    admitted_by: Optional[str] = None
    status: str = "new"


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


@dataclass
class Broadcast:
    broadcast_id: int
    event_id: str
    created_at: float
    author: Optional[str]
    kind: str
    target: str
    text: str
    status: str
    serial_response: Optional[str]


@dataclass
class Quest:
    quest_id: int
    event_id: str
    code: str
    title: str
    body: Optional[str]
    reward_rub: int
    hidden: int
    created_at: float


@dataclass
class MapBeacon:
    beacon_id: int
    event_id: str
    name: str
    kind: str
    x_pct: float
    y_pct: float
    note: Optional[str]


def _row_to_player(r) -> Player:
    keys = set(r.keys())
    return Player(
        player_id=r["player_id"],
        event_id=r["event_id"],
        name=r["name"],
        callsign=r["callsign"],
        group_name=r["group_name"],
        pda_uid=r["pda_uid"],
        registered_at=r["registered_at"],
        registered_by=r["registered_by"],
        notes=r["notes"],
        admitted_at=r["admitted_at"] if "admitted_at" in keys else None,
        admitted_by=r["admitted_by"] if "admitted_by" in keys else None,
        status=r["status"] if "status" in keys and r["status"] else "new",
    )


class EventDB:
    """Обёртка над SQLite-файлом одного события."""

    def __init__(self, path: str):
        self.path = path
        self.conn = sqlite3.connect(path)
        self.conn.row_factory = sqlite3.Row
        self.conn.execute("PRAGMA foreign_keys = ON")
        self.conn.executescript(SCHEMA)
        self._migrate()
        self.conn.commit()

    def _migrate(self):
        cols = {r[1] for r in self.conn.execute("PRAGMA table_info(players)")}
        if "admitted_at" not in cols:
            self.conn.execute("ALTER TABLE players ADD COLUMN admitted_at REAL")
        if "admitted_by" not in cols:
            self.conn.execute("ALTER TABLE players ADD COLUMN admitted_by TEXT")
        if "status" not in cols:
            self.conn.execute(
                "ALTER TABLE players ADD COLUMN status TEXT DEFAULT 'new'"
            )

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

    def rename_event(self, event_id: str, title: str):
        self.conn.execute(
            "UPDATE events SET title=? WHERE event_id=?",
            (title.strip(), event_id),
        )
        self.conn.commit()

    def close_event(self, event_id: str):
        self.conn.execute(
            "UPDATE events SET closed_at=? WHERE event_id=?",
            (time.time(), event_id),
        )
        self.conn.commit()

    def reopen_event(self, event_id: str):
        self.conn.execute(
            "UPDATE events SET closed_at=NULL WHERE event_id=?",
            (event_id,),
        )
        self.conn.commit()

    def is_closed(self, event_id: str) -> bool:
        row = self.get_event(event_id)
        return bool(row and row["closed_at"])

    # --- players ---------------------------------------------------------

    def add_player(self, event_id: str, name: str, callsign: str = "",
                   group_name: str = "", notes: str = "") -> int:
        cur = self.conn.execute(
            "INSERT INTO players(event_id, name, callsign, group_name, "
            "pda_uid, registered_at, registered_by, notes, status) "
            "VALUES (?, ?, ?, ?, NULL, NULL, NULL, ?, 'new')",
            (event_id, name.strip(), callsign.strip(), group_name.strip(),
             notes.strip()),
        )
        self.conn.commit()
        return cur.lastrowid

    def update_player(self, player_id: int, **fields):
        if not fields:
            return
        allowed = {
            "name", "callsign", "group_name", "notes", "pda_uid",
            "registered_at", "registered_by", "admitted_at", "admitted_by",
            "status",
        }
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
            status="registered",
        )

    def mark_registered(self, player_id: int, registered_by: str = "master",
                        pda_uid: Optional[str] = None) -> None:
        fields = {
            "registered_at": time.time(),
            "registered_by": registered_by,
            "status": "registered",
        }
        if pda_uid:
            fields["pda_uid"] = pda_uid.strip()
        self.update_player(player_id, **fields)

    def mark_admitted(self, player_id: int, admitted_by: str = "master") -> None:
        p = self.get_player(player_id)
        status = "admitted"
        fields = {
            "admitted_at": time.time(),
            "admitted_by": admitted_by,
            "status": status,
        }
        if p and not p.registered_at:
            fields["registered_at"] = time.time()
            fields["registered_by"] = admitted_by
        self.update_player(player_id, **fields)

    def delete_player(self, player_id: int):
        self.conn.execute("DELETE FROM player_stats WHERE player_id=?", (player_id,))
        self.conn.execute("DELETE FROM players WHERE player_id=?", (player_id,))
        self.conn.commit()

    def list_players(self, event_id: str, query: str = "", group_name: str = ""):
        sql = "SELECT * FROM players WHERE event_id=?"
        args = [event_id]
        if group_name.strip():
            sql += " AND group_name=?"
            args.append(group_name.strip())
        sql += " ORDER BY player_id"
        rows = self.conn.execute(sql, args).fetchall()
        players = [_row_to_player(r) for r in rows]
        q = query.strip().casefold()
        if q:
            def _hit(p):
                blob = " ".join(filter(None, [
                    p.name, p.callsign, p.group_name, p.pda_uid, p.notes,
                ])).casefold()
                return q in blob
            players = [p for p in players if _hit(p)]
        return players

    def list_groups(self, event_id: str):
        rows = self.conn.execute(
            "SELECT DISTINCT group_name FROM players "
            "WHERE event_id=? AND group_name IS NOT NULL AND group_name!='' "
            "ORDER BY group_name",
            (event_id,),
        ).fetchall()
        return [r[0] for r in rows]

    def player_counts(self, event_id: str) -> dict:
        total = self.conn.execute(
            "SELECT COUNT(*) FROM players WHERE event_id=?", (event_id,)
        ).fetchone()[0]
        registered = self.conn.execute(
            "SELECT COUNT(*) FROM players WHERE event_id=? AND status IN "
            "('registered','admitted')",
            (event_id,),
        ).fetchone()[0]
        admitted = self.conn.execute(
            "SELECT COUNT(*) FROM players WHERE event_id=? AND status='admitted'",
            (event_id,),
        ).fetchone()[0]
        bound = self.conn.execute(
            "SELECT COUNT(*) FROM players WHERE event_id=? AND pda_uid IS NOT NULL "
            "AND pda_uid!=''",
            (event_id,),
        ).fetchone()[0]
        return {
            "total": total,
            "registered": registered,
            "admitted": admitted,
            "bound": bound,
        }

    def get_player(self, player_id: int):
        r = self.conn.execute(
            "SELECT * FROM players WHERE player_id=?", (player_id,)
        ).fetchone()
        return _row_to_player(r) if r else None

    def find_by_pda_uid(self, event_id: str, pda_uid: str):
        r = self.conn.execute(
            "SELECT * FROM players WHERE event_id=? AND pda_uid=?",
            (event_id, pda_uid),
        ).fetchone()
        return _row_to_player(r) if r else None

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

    # --- broadcasts --------------------------------------------------------

    def add_broadcast(self, event_id: str, text: str, *, kind: str = "info",
                      target: str = "all", author: str = "master",
                      status: str = "queued", serial_response: str = "") -> int:
        kind = kind if kind in BROADCAST_KINDS else "info"
        status = status if status in BROADCAST_STATUSES else "queued"
        cur = self.conn.execute(
            "INSERT INTO broadcasts(event_id, created_at, author, kind, target, "
            "text, status, serial_response) VALUES (?,?,?,?,?,?,?,?)",
            (event_id, time.time(), author, kind, target or "all",
             text.strip(), status, serial_response or None),
        )
        self.conn.commit()
        return cur.lastrowid

    def update_broadcast(self, broadcast_id: int, **fields):
        allowed = {"status", "serial_response"}
        cols = [k for k in fields if k in allowed]
        if not cols:
            return
        set_clause = ", ".join(f"{c}=?" for c in cols)
        values = [fields[c] for c in cols] + [broadcast_id]
        self.conn.execute(
            f"UPDATE broadcasts SET {set_clause} WHERE broadcast_id=?", values
        )
        self.conn.commit()

    def list_broadcasts(self, event_id: str, limit: int = 200):
        rows = self.conn.execute(
            "SELECT * FROM broadcasts WHERE event_id=? "
            "ORDER BY created_at DESC LIMIT ?",
            (event_id, limit),
        ).fetchall()
        return [Broadcast(**{k: r[k] for k in r.keys()}) for r in rows]

    # --- quests ------------------------------------------------------------

    def add_quest(self, event_id: str, code: str, title: str, body: str = "",
                  reward_rub: int = 0, hidden: bool = False) -> int:
        cur = self.conn.execute(
            "INSERT INTO quests(event_id, code, title, body, reward_rub, hidden, "
            "created_at) VALUES (?,?,?,?,?,?,?)",
            (event_id, code.strip(), title.strip(), body.strip(),
             int(reward_rub), 1 if hidden else 0, time.time()),
        )
        self.conn.commit()
        return cur.lastrowid

    def update_quest(self, quest_id: int, **fields):
        allowed = {"code", "title", "body", "reward_rub", "hidden"}
        cols = [k for k in fields if k in allowed]
        if not cols:
            return
        set_clause = ", ".join(f"{c}=?" for c in cols)
        values = [fields[c] for c in cols] + [quest_id]
        self.conn.execute(
            f"UPDATE quests SET {set_clause} WHERE quest_id=?", values
        )
        self.conn.commit()

    def delete_quest(self, quest_id: int):
        self.conn.execute("DELETE FROM quests WHERE quest_id=?", (quest_id,))
        self.conn.commit()

    def list_quests(self, event_id: str):
        rows = self.conn.execute(
            "SELECT * FROM quests WHERE event_id=? ORDER BY hidden, code, quest_id",
            (event_id,),
        ).fetchall()
        return [Quest(**{k: r[k] for k in r.keys()}) for r in rows]

    def get_quest(self, quest_id: int):
        r = self.conn.execute(
            "SELECT * FROM quests WHERE quest_id=?", (quest_id,)
        ).fetchone()
        return Quest(**{k: r[k] for k in r.keys()}) if r else None

    # --- map ---------------------------------------------------------------

    def get_map_meta(self, event_id: str):
        return self.conn.execute(
            "SELECT * FROM map_meta WHERE event_id=?", (event_id,)
        ).fetchone()

    def set_map_image(self, event_id: str, src_path: str) -> str:
        """Скопировать скриншот рядом с .db и запомнить путь."""
        dest_dir = os.path.join(os.path.dirname(self.path), f"map_{event_id}")
        os.makedirs(dest_dir, exist_ok=True)
        ext = os.path.splitext(src_path)[1].lower() or ".png"
        if ext not in (".png", ".gif", ".jpg", ".jpeg", ".webp"):
            ext = ".png"
        dest = os.path.join(dest_dir, "background" + ext)
        shutil.copy2(src_path, dest)
        self.conn.execute(
            "INSERT INTO map_meta(event_id, image_path, updated_at) VALUES (?,?,?) "
            "ON CONFLICT(event_id) DO UPDATE SET image_path=excluded.image_path, "
            "updated_at=excluded.updated_at",
            (event_id, dest, time.time()),
        )
        self.conn.commit()
        return dest

    def clear_map_image(self, event_id: str):
        self.conn.execute(
            "INSERT INTO map_meta(event_id, image_path, updated_at) VALUES (?,?,?) "
            "ON CONFLICT(event_id) DO UPDATE SET image_path=NULL, "
            "updated_at=excluded.updated_at",
            (event_id, None, time.time()),
        )
        self.conn.commit()

    def add_beacon(self, event_id: str, name: str, kind: str,
                   x_pct: float, y_pct: float, note: str = "") -> int:
        kind = kind if kind in BEACON_KINDS else "other"
        x_pct = max(0.0, min(100.0, float(x_pct)))
        y_pct = max(0.0, min(100.0, float(y_pct)))
        cur = self.conn.execute(
            "INSERT INTO map_beacons(event_id, name, kind, x_pct, y_pct, note) "
            "VALUES (?,?,?,?,?,?)",
            (event_id, name.strip() or "маяк", kind, x_pct, y_pct,
             (note or "").strip()),
        )
        self.conn.commit()
        return cur.lastrowid

    def update_beacon(self, beacon_id: int, **fields):
        allowed = {"name", "kind", "x_pct", "y_pct", "note"}
        cols = [k for k in fields if k in allowed]
        if not cols:
            return
        if "kind" in fields and fields["kind"] not in BEACON_KINDS:
            fields = dict(fields)
            fields["kind"] = "other"
        set_clause = ", ".join(f"{c}=?" for c in cols)
        values = [fields[c] for c in cols] + [beacon_id]
        self.conn.execute(
            f"UPDATE map_beacons SET {set_clause} WHERE beacon_id=?", values
        )
        self.conn.commit()

    def delete_beacon(self, beacon_id: int):
        self.conn.execute("DELETE FROM map_beacons WHERE beacon_id=?", (beacon_id,))
        self.conn.commit()

    def list_beacons(self, event_id: str):
        rows = self.conn.execute(
            "SELECT * FROM map_beacons WHERE event_id=? ORDER BY beacon_id",
            (event_id,),
        ).fetchall()
        return [MapBeacon(**{k: r[k] for k in r.keys()}) for r in rows]

    # --- export / import ---------------------------------------------------

    def export_players_csv(self, event_id: str, csv_path: str):
        players = self.list_players(event_id)
        with open(csv_path, "w", newline="", encoding="utf-8-sig") as f:
            w = csv.writer(f)
            w.writerow(["player_id", "name", "callsign", "group_name",
                        "pda_uid", "status", "registered_at", "registered_by",
                        "admitted_at", "admitted_by", "notes"])
            for p in players:
                w.writerow([
                    p.player_id, p.name, p.callsign or "", p.group_name or "",
                    p.pda_uid or "", p.status,
                    p.registered_at or "", p.registered_by or "",
                    p.admitted_at or "", p.admitted_by or "", p.notes or "",
                ])

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
                w.writerow([
                    s.player_id, p.name if p else "", p.callsign if p else "",
                    time.strftime("%Y-%m-%d %H:%M:%S",
                                  time.localtime(s.collected_at)),
                    s.level, s.xp, s.money_rub, s.deaths,
                    s.cheat_shield_count, s.rank_title or "",
                ])

    def export_quests_csv(self, event_id: str, csv_path: str):
        quests = self.list_quests(event_id)
        with open(csv_path, "w", newline="", encoding="utf-8-sig") as f:
            w = csv.writer(f)
            w.writerow(["code", "title", "body", "reward_rub", "hidden"])
            for q in quests:
                w.writerow([q.code, q.title, q.body or "", q.reward_rub,
                            "1" if q.hidden else "0"])

    def export_db_copy(self, dest_path: str):
        self.conn.commit()
        shutil.copy2(self.path, dest_path)

    @staticmethod
    def import_db_copy(src_path: str, dest_dir: str) -> str:
        os.makedirs(dest_dir, exist_ok=True)
        name = os.path.basename(src_path)
        if not name.endswith(".db"):
            name += ".db"
        dest = os.path.join(dest_dir, name)
        if os.path.exists(dest):
            stem, ext = os.path.splitext(name)
            dest = os.path.join(dest_dir, f"{stem}_imported_{new_event_id()}{ext}")
        shutil.copy2(src_path, dest)
        return dest
