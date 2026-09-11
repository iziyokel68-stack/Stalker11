"""
Каналы мастер ↔ ПДА (канон).
USB Serial — только к одному устройству на ПК/телефоне мастера.
Доставка до игрока: LoRa по № регистрации или общий EEPROM-чип.
"""

import os
import sys

PROGRAMMER_DIR = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "programmat_pc")
)
if PROGRAMMER_DIR not in sys.path:
    sys.path.insert(0, PROGRAMMER_DIR)

from serial_link import (
    build_admit_chip,
    build_lora_cmd,
    build_register_chip,
    build_revive_chip,
    emission_seconds,
)

# Msg.COMMAND val1 = CmdSub (protocol.py)
CMD_KILL = 0
CMD_REVIVE = 1
CMD_ADMIT = 7

EEPROM_DEVICES = {"CHIP_BOX", "TERMINAL", "CASHIER"}
LORA_DEVICES = {"CHIP_BOX", "PDA", "ANOMALY", "SAFE_ZONE", "MASTER_PULT", "PULT"}


def player_id_from_target(target: str) -> int:
    """Только ID игрока события. 0 / 'все' = всем. Имена в LoRa не ходят."""
    raw = (target or "0").strip().lower()
    if not raw or raw in ("all", "все", "0") or raw.startswith("0 "):
        return 0
    if raw.startswith("player:"):
        try:
            return int(raw.split(":")[1])
        except (IndexError, ValueError):
            return 0
    token = raw.split()[0]
    try:
        return int(token)
    except ValueError:
        return 0


def id_choices(players) -> list:
    """Список целей LoRa: только ID (имена — в карточке, не в эфире)."""
    values = ["0"]
    for p in players:
        values.append(str(p.player_id))
    return values


def lora_emission(player_id: int, timer_min: int, duration_min: int) -> str:
    v1, v2 = emission_seconds(timer_min, duration_min)
    return build_lora_cmd(player_id, "EMISSION", v1, v2)


def lora_radio(player_id: int, track: int) -> str:
    """Радио: только трек. Громкость — кнопки на ПДА."""
    return build_lora_cmd(player_id, "RADIO", int(track), 0)


def lora_broadcast(player_id: int, text: str) -> str:
    return build_lora_cmd(player_id, "BROADCAST", 0, 0, text=text)


def lora_command(player_id: int, cmd_sub: int, param: int = 0) -> str:
    return build_lora_cmd(player_id, "COMMAND", int(cmd_sub), int(param))


def eeprom_register(player_id: int, name: str) -> str:
    return build_register_chip(player_id, name)


def eeprom_admit() -> str:
    return build_admit_chip()


def eeprom_revive() -> str:
    return build_revive_chip()
