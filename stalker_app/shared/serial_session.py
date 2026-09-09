"""
Общий USB Serial для модулей приложения мастера.
Mutex COM: программатор (pygame) и модули не держат порт одновременно.
docs/PROGRESSION.txt §10.4
"""

import os
import sys
import threading

PROGRAMMER_DIR = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "programmat_pc")
)
if PROGRAMMER_DIR not in sys.path:
    sys.path.insert(0, PROGRAMMER_DIR)

from serial_link import (  # noqa: E402
    SERIAL_AVAILABLE,
    SerialLink,
    build_broadcast_cmd,
    build_emission_cmd,
    build_radio_cmd,
    build_register_cmd,
    list_port_names,
)


class SerialSession:
    """Один SerialLink на всё приложение + блокировка под программатор."""

    def __init__(self):
        self.link = SerialLink()
        self._lock = threading.Lock()
        self._held_by = None  # "app" | "programmer" | None

    @property
    def available(self) -> bool:
        return SERIAL_AVAILABLE

    @property
    def connected(self) -> bool:
        return self.link.connected

    def status_text(self) -> str:
        if not SERIAL_AVAILABLE:
            return "pyserial не установлен"
        if self._held_by == "programmer":
            return "COM занят программатором"
        return self.link.status_text()

    def port_names(self):
        names = list_port_names()
        return ["AUTO"] + names if names else ["AUTO"]

    def connect(self, port: str = "AUTO"):
        with self._lock:
            if self._held_by == "programmer":
                return False, "Сначала закройте окно программатора"
            ok = self.link.connect_sync(port)
            if ok:
                self._held_by = "app"
                return True, self.link.status_text()
            return False, self.link.last_error or "Нет устройства"

    def disconnect(self):
        with self._lock:
            self.link.disconnect()
            if self._held_by == "app":
                self._held_by = None

    def release_for_programmer(self):
        """Освободить COM перед запуском programmer.py."""
        with self._lock:
            self.link.disconnect()
            self._held_by = "programmer"

    def programmer_closed(self):
        with self._lock:
            if self._held_by == "programmer":
                self._held_by = None

    def _need_pda(self):
        if not self.link.connected:
            return "ПДА не подключён (USB Serial)"
        if self.link.dev_type != "PDA":
            return f"Нужен ПДА, сейчас {self.link.dev_type or 'нет устройства'}"
        return None

    def register_player(self, name, callsign="", group=""):
        err = self._need_pda()
        if err:
            return False, err, None
        cmd = build_register_cmd(name, callsign, group)
        ok, resp = self.link.query_ok(cmd)
        uid = None
        uid_line = self.link.query("CONFIG:UID", timeout=1.5)
        if uid_line and uid_line.startswith("UID:"):
            uid = uid_line.split(":", 1)[1].strip()
        return ok, resp or self.link.last_error, uid

    def admit(self):
        err = self._need_pda()
        if err:
            return False, err
        return self.link.query_ok("CONFIG:ADMIT")

    def revive(self):
        err = self._need_pda()
        if err:
            return False, err
        return self.link.query_ok("CONFIG:REVIVE")

    def broadcast(self, text: str):
        err = self._need_pda()
        if err:
            return False, err
        return self.link.query_ok(build_broadcast_cmd(text))

    def emission(self, timer_sec: int, duration_sec: int):
        if not self.link.connected:
            return False, "Устройство не подключено"
        return self.link.query_ok(build_emission_cmd(timer_sec, duration_sec))

    def radio(self, track: int, volume: int = 0):
        if not self.link.connected:
            return False, "Устройство не подключено"
        return self.link.query_ok(build_radio_cmd(track, volume))

    def read_uid(self):
        err = self._need_pda()
        if err:
            return None, err
        line = self.link.query("CONFIG:UID", timeout=1.5)
        if line and line.startswith("UID:"):
            return line.split(":", 1)[1].strip(), line
        return None, line or "Нет UID"

    def read_snapshot(self):
        err = self._need_pda()
        if err:
            return None, err
        return self.link.read_pda_snapshot(), "ok"
