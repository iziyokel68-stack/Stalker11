"""
Общий USB Serial для модулей приложения мастера.
Одно USB-устройство на ПК мастера: стол CHIP_BOX (чипы + регистрация + LoRa-пульт).
ПДА игрока в поле — LoRa по № или EEPROM-чип, не кабель к каждому ПДА.
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
    list_port_names,
    parse_device_id,
)

from shared.master_channel import (  # noqa: E402
    EEPROM_DEVICES,
    lora_broadcast,
    lora_command,
    lora_emission,
    lora_radio,
    eeprom_admit,
    eeprom_register,
    eeprom_revive,
    CMD_ADMIT,
    CMD_KILL,
    CMD_REVIVE,
)

TXN_OP_REGISTER = 5


class SerialSession:
    """Один SerialLink на всё приложение (программатор внутри того же окна)."""

    def __init__(self):
        self.link = SerialLink()
        self._lock = threading.Lock()

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
            return "Подключите стол мастера по USB (CHIP_BOX)"
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
                "На этом ESP нет Ra-01 (LoRa не поднялся). "
                "Стол мастера должен быть S3/C3 + модуль Ra-01. "
                "Команда сохранена в журнале."
            )
        return ok, resp or self.link.last_error

    def emission(self, timer_min: int, duration_min: int, player_id: int = 0):
        return self.lora_tx(lora_emission(player_id, timer_min, duration_min))

    def radio(self, track: int, player_id: int = 0):
        return self.lora_tx(lora_radio(player_id, track))

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

    def register_bridge(self, player_id: int, name: str, uid: str):
        """Мост: CHIP_BOX пишет чип + TXN; ПДА на шнуре забирает ID и UID."""
        err = self._need_eeprom()
        if err:
            return False, err
        ok, msg = self.write_register_chip(player_id, name)
        if not ok:
            return False, msg
        resp = self.link.txn_start(
            amount=0, item_id=player_id, op=TXN_OP_REGISTER, quest_id=uid,
        )
        if not resp:
            return False, self.link.last_error or "Нет ответа TXN"
        if str(resp).startswith("ERROR"):
            return False, resp
        return True, resp

    def poll_txn(self):
        err = self._need_eeprom()
        if err:
            return None, err
        return self.link.txn_status(), "ok"

    def field_device_id(self):
        """ID маяка/полевого устройства на USB (при включении шлёт id=)."""
        err = self._need_usb()
        if err:
            return None, err
        if getattr(self.link, "dev_id", None):
            return self.link.dev_id, "ok"
        for cmd in ("STALKER_WHO", "CONFIG:UID"):
            line = self.link.query(cmd, timeout=1.5)
            parsed = parse_device_id(line or "")
            if parsed:
                self.link.dev_id = parsed
                return parsed, line
        return None, "Нет ID — подключите аномалию или убежище по USB"

    def flash_quest_board(self, cards):
        """USB → терминал QUEST: записать каталог на кассету EEPROM."""
        err = self._need_usb()
        if err:
            return False, err
        if self.link.dev_type != "TERMINAL":
            return False, (
                f"Подключите терминал QUEST по USB (сейчас {self.link.dev_type})."
            )
        from quest_catalog import QuestCard, format_quest_add, QUEST_CAT_MAX

        packed = []
        for c in cards[:QUEST_CAT_MAX]:
            if isinstance(c, QuestCard):
                packed.append(c)
            else:
                packed.append(QuestCard(
                    code=getattr(c, "code", "") or "",
                    title=getattr(c, "title", "") or "",
                    rub=int(getattr(c, "reward_rub", 0) or 0),
                    hidden=bool(getattr(c, "hidden", False)),
                    mode=getattr(c, "claim_mode", "timeout") or "timeout",
                    timeout_min=int(getattr(c, "timeout_min", 120) or 120),
                ))
        ok_role, msg_role = self.set_terminal_role("QUEST")
        if not ok_role:
            return False, msg_role or "Не удалось выставить роль QUEST"
        cmds = [format_quest_add(c) for c in packed]
        ok, resp = self.link.flash_quest_catalog(cmds)
        if not ok:
            return False, resp or self.link.last_error or "ошибка прошивки доски"
        extra = ""
        if len(cards) > QUEST_CAT_MAX:
            extra = f" (первые {QUEST_CAT_MAX} из {len(cards)})"
        return True, (resp or "OK") + extra

    def configure_terminal(self, role: str, limit_purchase: int = 0,
                           limit_withdraw: int = 0, limit_deposit: int = 0):
        """Роль + лимиты кассы/банкомата. Прошивку .ino не трогает."""
        err = self._need_usb()
        if err:
            return False, err
        if self.link.dev_type != "TERMINAL":
            return False, (
                f"Подключите универсальный терминал по USB "
                f"(сейчас {self.link.dev_type})."
            )
        ok, msg = self.set_terminal_role(role)
        if not ok:
            return False, msg or "Не удалось выставить роль"
        ok, resp = self.link.terminal_cfg_set(
            limit_purchase=limit_purchase,
            limit_withdraw=limit_withdraw,
            limit_deposit=limit_deposit,
            role=role,
        )
        if not ok:
            return False, resp or self.link.last_error or "Нет TERMINAL_CFG — обновите .ino терминала"
        return True, resp
