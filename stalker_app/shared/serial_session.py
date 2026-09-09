"""
Общий USB Serial для модулей приложения мастера.
Одно устройство на ПК (CHIP_BOX / полевое / Мастер-Пульт).
ПДА игрока — LoRa по № или EEPROM-чип, не кабель к каждому ПДА.
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
    build_volume_cmd,
    list_port_names,
)

from shared.master_channel import (  # noqa: E402
    EEPROM_DEVICES,
    lora_broadcast,
    lora_command,
    lora_emission,
    lora_radio,
    lora_volume,
    eeprom_admit,
    eeprom_register,
    eeprom_revive,
    CMD_ADMIT,
    CMD_KILL,
    CMD_REVIVE,
)


class SerialSession:
    """Один SerialLink на всё приложение (программатор внутри того же окна)."""

    def __init__(self):
        self.link = SerialLink()
        self._lock = threading.Lock()
        self.volume = 20

    @property
    def available(self) -> bool:
        return SERIAL_AVAILABLE

    @property
    def connected(self) -> bool:
        return self.link.connected

    @property
    def dev_type(self):
        return self.link.dev_type

    def status_text(self) -> str:
        if not SERIAL_AVAILABLE:
            return "pyserial не установлен"
        return self.link.status_text()

    def port_names(self):
        names = list_port_names()
        return ["AUTO"] + names if names else ["AUTO"]

    def connect(self, port: str = "AUTO"):
        with self._lock:
            ok = self.link.connect_sync(port)
            if ok:
                return True, self.link.status_text()
            return False, self.link.last_error or "Нет устройства"

    def disconnect(self):
        with self._lock:
            self.link.disconnect()

    def _need_usb(self):
        if not self.link.connected:
            return "Подключите устройство мастера по USB (CHIP_BOX / Мастер-Пульт)"
        return None

    def _need_eeprom(self):
        err = self._need_usb()
        if err:
            return err
        if self.link.dev_type not in EEPROM_DEVICES:
            return (
                f"Для чипа нужен CHIP_BOX (сейчас {self.link.dev_type}). "
                "ПДА игрока по USB не подключают."
            )
        return None

    def flash_config(self, cfg_str: str):
        """CONFIG_WRITE или готовая CONFIG:* строка (FUNC/PRESET)."""
        err = self._need_usb()
        if err:
            return False, err
        if not cfg_str:
            return False, "Пустая конфигурация"
        if cfg_str.startswith("CONFIG:"):
            return self.link.query_ok(cfg_str)
        ok = self.link.write_config(cfg_str)
        return ok, "OK:WRITTEN" if ok else (self.link.last_error or "ошибка записи")

    def set_terminal_role(self, role: str):
        err = self._need_usb()
        if err:
            return False, err
        return self.link.terminal_role_set(role)

    def read_config(self):
        err = self._need_usb()
        if err:
            return None, err
        return self.link.read_config(), "ok"

    def lora_tx(self, cmd: str):
        err = self._need_usb()
        if err:
            return False, err
        ok, resp = self.link.query_ok(cmd)
        if not ok and resp and "NO_LORA" in resp:
            return False, (
                "На этом USB нет LoRa (CHIP_BOX/терминал). "
                "Нужен Мастер-Пульт. Команда сохранена в журнале."
            )
        return ok, resp or self.link.last_error

    def emission(self, timer_min: int, duration_min: int, player_id: int = 0):
        return self.lora_tx(lora_emission(player_id, timer_min, duration_min))

    def radio(self, track: int, player_id: int = 0):
        return self.lora_tx(lora_radio(player_id, track, self.volume))

    def set_volume(self, level: int, player_id: int = 0):
        self.volume = max(0, min(30, int(level)))
        usb = self._need_usb()
        if usb:
            return False, usb
        # Стендовый ПДА на кабеле программатора — сразу DFPlayer.
        if self.link.dev_type == "PDA":
            return self.link.query_ok(build_volume_cmd(self.volume))
        return self.lora_tx(lora_volume(player_id, self.volume))

    def broadcast(self, text: str, player_id: int = 0):
        return self.lora_tx(lora_broadcast(player_id, text))

    def command_lora(self, cmd_sub: int, player_id: int = 0, param: int = 0):
        return self.lora_tx(lora_command(player_id, cmd_sub, param))

    def admit_lora(self, player_id: int):
        return self.command_lora(CMD_ADMIT, player_id)

    def revive_lora(self, player_id: int):
        return self.command_lora(CMD_REVIVE, player_id)

    def kill_lora(self, player_id: int):
        return self.command_lora(CMD_KILL, player_id)

    def write_register_chip(self, player_id: int, name: str):
        err = self._need_eeprom()
        if err:
            return False, err
        return self.flash_config(eeprom_register(player_id, name))

    def write_admit_chip(self):
        err = self._need_eeprom()
        if err:
            return False, err
        return self.flash_config(eeprom_admit())

    def write_revive_chip(self):
        err = self._need_eeprom()
        if err:
            return False, err
        return self.flash_config(eeprom_revive())
