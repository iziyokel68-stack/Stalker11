"""
USB Serial к устройствам STALKER (ESP32).
================================================
Handshake: PC → STALKER_WHO  /  ESP → STALKER:TYPE:ver[,role=…]

USB — только к одному устройству на ПК мастера (CHIP_BOX, полевое устройство
на столе, позже Мастер-Пульт / телефон). Взаимодействие с ПДА игрока:
  • LoRa по номеру регистрации (player_id, 0 = все) — LORA_TX:…
  • общий EEPROM-чип через CHIP_BOX — CONFIG_WRITE / TXN_*

Команды:
  CONFIG_WRITE:type=…   прошивка чипа / аномалии / убежища
  CONFIG:FUNC: / CONFIG:PRESET:   стендовая прошивка ПДА
  CONFIG:EMISSION:timer_min=…,duration_min=…
  CONFIG:RADIO:track=N            радио, только трек
  CONFIG:VOLUME:level=0..30       стенд; у игрока громкость в НАСТРОЙКАХ ПДА
  LORA_TX:to=N,msg=…,v1=…,v2=…[,text=…]
  name= в CONFIG_WRITE → EEPROM @0x26 (чип регистрации)

Модуль без pygame — его импортируют и программатор, и приложение мастера.
"""

import time
import threading

try:
    import serial
    import serial.tools.list_ports
    SERIAL_AVAILABLE = True
except ImportError:
    serial = None
    SERIAL_AVAILABLE = False

DEVICE_TYPE_MAP = {
    "CHIP_BOX": 0,
    "ANOMALY": 1,
    "SAFE_ZONE": 2,
    "PDA": 3,
    "TERMINAL": 4,
    "CASHIER": 4,
}


def list_port_names():
    """Имена живых COM/tty-портов (пусто, если pyserial нет)."""
    if not SERIAL_AVAILABLE:
        return []
    return [p.device for p in serial.tools.list_ports.comports()]


class SerialLink:
    """Управляет Serial-соединением с устройством."""
    BAUD = 115200
    TIMEOUT = 1.5
    BOOT_WAIT = 4.0
    HANDSHAKE_WAIT = 2.5

    def __init__(self):
        self.port = None
        self.ser = None
        self.dev_type = None
        self.dev_ver = None
        self.terminal_role = None
        self.dev_id = None
        self.scanning = False
        self.last_error = ""
        self._lock = threading.Lock()

    @property
    def connected(self):
        return self.ser is not None and self.ser.is_open

    def disconnect(self):
        with self._lock:
            if self.ser is not None:
                try:
                    self.ser.close()
                except Exception:
                    pass
            self.ser = None
            self.port = None
            self.dev_type = None
            self.dev_ver = None
            self.terminal_role = None
            self.dev_id = None
            self.last_error = ""

    def _parse_who_line(self, line):
        parts = line.split(":", 2)
        if len(parts) < 2:
            return None, None, None, None
        dtype = parts[1].strip()
        if dtype not in DEVICE_TYPE_MAP:
            return None, None, None, None
        rest = parts[2] if len(parts) > 2 else "?"
        ver = rest.split(",")[0].strip() or "?"
        role = None
        dev_id = None
        for token in rest.split(","):
            token = token.strip()
            if token.startswith("role="):
                role = token.split("=", 1)[1].strip()
            elif token.startswith("id="):
                dev_id = token.split("=", 1)[1].strip()
        return dtype, ver, role, dev_id

    def _read_terminal_role(self, s):
        try:
            s.reset_input_buffer()
            s.write(b"TERMINAL_ROLE\n")
            deadline = time.time() + self.TIMEOUT
            while time.time() < deadline:
                line = s.readline().decode(errors="ignore").strip()
                if line.startswith("TERMINAL_ROLE:"):
                    return line.split(":", 1)[1].split(",")[0].strip()
                if line.startswith("OK:TERMINAL_ROLE:"):
                    return line.split(":", 2)[2].split(",")[0].strip()
        except Exception:
            pass
        return None

    def _finish_handshake(self, s, line, boot_role=None):
        dtype, ver, role, dev_id = self._parse_who_line(line)
        if not dtype:
            return None
        if not role and boot_role:
            role = boot_role
        if dtype == "TERMINAL" and not role:
            role = self._read_terminal_role(s)
        return dtype, ver, role, dev_id

    def _wait_stalker_who(self, s, boot_role=None):
        deadline = time.time() + self.HANDSHAKE_WAIT
        while time.time() < deadline:
            line = s.readline().decode(errors="ignore").strip()
            if not line:
                continue
            if line.startswith("STALKER:"):
                parsed = self._finish_handshake(s, line, boot_role)
                if parsed:
                    return parsed
        if boot_role:
            return "TERMINAL", "v1", boot_role, None
        return None

    def _try_port(self, portname):
        try:
            s = serial.Serial(portname, self.BAUD, timeout=0.2)
            time.sleep(0.2)
            s.reset_input_buffer()

            boot_role = None
            boot_end = time.time() + self.BOOT_WAIT
            while time.time() < boot_end:
                line = s.readline().decode(errors="ignore").strip()
                if not line:
                    continue
                if line.startswith("TERMINAL_ROLE:"):
                    boot_role = line.split(":", 1)[1].split(",")[0].strip()
                elif line.startswith("STALKER:"):
                    parsed = self._finish_handshake(s, line, boot_role)
                    if parsed:
                        return s, *parsed
                elif line == "READY" or line.endswith(" READY"):
                    break

            s.write(b"STALKER_WHO\n")
            parsed = self._wait_stalker_who(s, boot_role)
            if parsed:
                return s, *parsed
            s.close()
        except Exception:
            pass
        return None, None, None, None, None

    def connect_sync(self, target_port="AUTO"):
        if not SERIAL_AVAILABLE:
            self.last_error = "pyserial не установлен"
            return False
        if self.connected:
            self.disconnect()
        ports = (list_port_names() if target_port == "AUTO" else [target_port])
        for p in ports:
            s, dtype, dver, role, dev_id = self._try_port(p)
            if s:
                self.ser = s
                self.port = p
                self.dev_type = dtype
                self.dev_ver = dver
                self.terminal_role = role
                self.dev_id = dev_id
                self.last_error = ""
                return True
        self.last_error = "Устройство не найдено"
        return False

    def scan(self, target_port="AUTO"):
        if self.scanning:
            return

        def _worker():
            self.scanning = True
            with self._lock:
                if self.connected:
                    try:
                        self.ser.close()
                    except Exception:
                        pass
                    self.ser = None
                self.dev_type = None
                self.port = None
                self.terminal_role = None
                self.dev_id = None

            ports = list_port_names() if target_port == "AUTO" else [target_port]
            for p in ports:
                s, dtype, dver, role, dev_id = self._try_port(p)
                if s:
                    with self._lock:
                        self.ser = s
                        self.port = p
                        self.dev_type = dtype
                        self.dev_ver = dver
                        self.terminal_role = role
                        self.dev_id = dev_id
                    self.scanning = False
                    return
            self.scanning = False

        threading.Thread(target=_worker, daemon=True).start()

    def send(self, cmd: str):
        if not self.connected:
            return False
        try:
            with self._lock:
                self.ser.write((cmd + "\n").encode("utf-8"))
            return True
        except Exception:
            self.ser = None
            return False

    def query(self, cmd: str, timeout=2.0):
        if not self.send(cmd):
            return None
        try:
            self.ser.timeout = timeout
            return self.ser.readline().decode(errors="ignore").strip()
        except Exception:
            return None

    def query_lines(self, cmd: str, timeout=2.0):
        """Отправить команду и собрать несколько строк ответа (CONFIG_READ)."""
        if not self.send(cmd):
            return []
        lines = []
        deadline = time.time() + timeout
        try:
            self.ser.timeout = 0.12
            while time.time() < deadline:
                line = self.ser.readline().decode(errors="ignore").strip()
                if line:
                    lines.append(line)
                elif lines:
                    break
        except Exception:
            pass
        return lines

    def query_ok(self, cmd: str, timeout=2.0):
        """True, если ответ начинается с OK (или равен OK)."""
        resp = self.query(cmd, timeout=timeout)
        if resp and (resp == "OK" or resp.startswith("OK")):
            self.last_error = ""
            return True, resp
        self.last_error = resp or "Нет ответа"
        return False, resp

    def read_config(self):
        resp = self.query("CONFIG_READ")
        if not resp or not resp.startswith("CONFIG:"):
            lines = [resp] if resp else []
            extra = self.query_lines("CONFIG_READ") if not resp else []
            blob = " ".join(x for x in (lines + extra) if x)
            result = {}
            for token in blob.replace(" ", ",").split(","):
                if "=" in token:
                    k, v = token.split("=", 1)
                    k = k.split(":")[-1].strip()
                    try:
                        result[k] = int(v)
                    except ValueError:
                        result[k] = v
            return result or None
        result = {}
        for token in resp[7:].split(","):
            if "=" in token:
                k, v = token.split("=", 1)
                try:
                    result[k.strip()] = int(v)
                except ValueError:
                    result[k.strip()] = v
        return result

    def read_pda_snapshot(self):
        """Разобрать многострочный CONFIG_READ ПДА → dict."""
        lines = self.query_lines("CONFIG_READ", timeout=2.5)
        snap = {"raw": lines}
        for line in lines:
            if ":" in line:
                prefix, rest = line.split(":", 1)
                prefix = prefix.strip().upper()
            else:
                prefix, rest = "", line
            for token in rest.split(","):
                if "=" not in token:
                    continue
                k, v = token.split("=", 1)
                key = k.strip()
                if prefix and key.lower() not in ("flags",):
                    pass
                try:
                    snap[key.strip()] = int(v.strip())
                except ValueError:
                    snap[key.strip()] = v.strip()
            if line.startswith("UID:"):
                snap["uid"] = line.split(":", 1)[1].strip()
            if line.startswith("NAME:"):
                snap["name"] = line.split(":", 1)[1].strip()
        uid_line = self.query("CONFIG:UID", timeout=1.5)
        if uid_line and uid_line.startswith("UID:"):
            snap["uid"] = uid_line.split(":", 1)[1].strip()
        return snap

    def write_config(self, cfg_str: str):
        if not self.send(f"CONFIG_WRITE:{cfg_str}"):
            self.last_error = "Порт закрыт"
            return False
        try:
            self.ser.timeout = 0.3
            deadline = time.time() + 8.0
            while time.time() < deadline:
                line = self.ser.readline().decode(errors="ignore").strip()
                if not line:
                    continue
                if line.startswith("OK:WRITTEN") or line == "OK":
                    self.last_error = ""
                    return True
                if line.startswith("ERROR:"):
                    self.last_error = line
                    return False
            self.last_error = "Нет ответа OK:WRITTEN (таймаут)"
            return False
        except Exception as exc:
            self.last_error = str(exc)
            return False

    def txn_start(self, amount: int = 0, item_id: int = 0, txn_id: int = 0,
                   op: int = 0, quest_id: str = ""):
        cmd = f"TXN_START:amount={amount},item={item_id},op={int(op)}"
        if txn_id > 0:
            cmd += f",txn_id={txn_id}"
        if quest_id:
            cmd += ",quest_id=" + str(quest_id).replace(",", " ")[:8]
        return self.query(cmd, timeout=3.0)

    def txn_status(self):
        return self.query("TXN_STATUS", timeout=2.0)

    def txn_wait(self, timeout_ms: int = 30000):
        return self.query(f"TXN_WAIT:timeout_ms={timeout_ms}",
                          timeout=timeout_ms / 1000.0 + 3.0)

    def txn_reset(self):
        return self.query("TXN_RESET", timeout=2.0)

    def terminal_role_set(self, role: str):
        resp = self.query(f"TERMINAL_ROLE:{role}", timeout=2.0)
        if resp and resp.startswith("OK:TERMINAL_ROLE:"):
            self.terminal_role = role
            return True, resp
        return False, resp

    def terminal_role_get(self):
        resp = self.query("TERMINAL_ROLE", timeout=2.0)
        if resp and resp.startswith("TERMINAL_ROLE:"):
            role = resp.split(":", 1)[1].split(",")[0].strip()
            self.terminal_role = role
            return role
        return self.terminal_role

    def status_text(self):
        if self.scanning:
            return "[..] Поиск..."
        if self.connected:
            extra = f" role={self.terminal_role}" if self.terminal_role else ""
            devid = f" id={self.dev_id}" if self.dev_id else ""
            return f"[OK] {self.port}  [{self.dev_type} {self.dev_ver}{extra}{devid}]"
        return "[X]  Не подключено"

    def status_color(self):
        if self.scanning:
            return (200, 200, 80)
        if self.connected:
            return (80, 220, 120)
        return (180, 80, 80)


def emission_seconds(timer_min: int, duration_min: int):
    """Минуты UI → секунды int16 для LoRa Msg.EMISSION (val1/val2)."""
    timer = max(0, min(32767, int(timer_min) * 60))
    duration = max(0, min(32767, int(duration_min) * 60))
    return timer, duration


def build_register_cmd(name: str, callsign: str = "", group: str = "") -> str:
    """Стенд: CONFIG:REGISTER. Канон поля — EEPROM-чип регистрации / LoRa №."""
    parts = [f"name={_sanitize_field(name)}"]
    if callsign.strip():
        parts.append(f"callsign={_sanitize_field(callsign)}")
    if group.strip():
        parts.append(f"group={_sanitize_field(group)}")
    return "CONFIG:REGISTER:" + ",".join(parts)


def build_broadcast_cmd(text: str) -> str:
    return "CONFIG:BROADCAST:" + (text or "").replace("\n", " ").strip()[:80]


def build_emission_cmd(timer_min: int, duration_min: int) -> str:
    """Выброс: минуты в UI, на провод — timer_min / duration_min."""
    return (
        f"CONFIG:EMISSION:timer_min={int(timer_min)},"
        f"duration_min={int(duration_min)}"
    )


def parse_device_id(line: str):
    """id= из STALKER_WHO / BEACON: / UID: (маяк при включении)."""
    if not line:
        return None
    raw = str(line).strip()
    for token in raw.replace(":", ",").split(","):
        token = token.strip()
        if token.startswith("id="):
            val = token.split("=", 1)[1].strip()
            return val or None
    if raw.startswith("UID:"):
        return raw.split(":", 1)[1].split(",")[0].strip() or None
    if raw.startswith("BEACON:"):
        rest = raw.split(":", 1)[1].strip()
        if rest.startswith("id="):
            rest = rest[3:]
        return rest.split(",")[0].strip() or None
    return None


def build_radio_cmd(track: int, volume: int = 0) -> str:
    """Радио: только трек. Громкость — кнопки на ПДА, не мастер."""
    return f"CONFIG:RADIO:track={int(track)}"


def build_volume_cmd(level: int) -> str:
    level = max(0, min(30, int(level)))
    return f"CONFIG:VOLUME:level={level}"


def build_lora_cmd(player_id: int, msg: str, val1: int = 0, val2: int = 0,
                   text: str = "") -> str:
    """Команда Мастер-Пульту: LoRa на номер регистрации (0 = все)."""
    cmd = f"LORA_TX:to={int(player_id)},msg={msg},v1={int(val1)},v2={int(val2)}"
    if text:
        cmd += ",text=" + text.replace("\n", " ").replace(",", " ")[:48]
    return cmd


def build_register_chip(player_id: int, name: str) -> str:
    """Админ-чип регистрации (type=3 sub=7), имя в расширении EEPROM @0x26."""
    nm = _sanitize_field(name)
    return f"type=3,sub=7,uses=1,p0={int(player_id)},name={nm}"


def build_admit_chip() -> str:
    return "type=3,sub=6,uses=1," + ",".join(f"p{i}=0" for i in range(16))


def build_revive_chip() -> str:
    return "type=3,sub=0,uses=1," + ",".join(f"p{i}=0" for i in range(16))


def _sanitize_field(value: str) -> str:
    return (value or "").replace(",", " ").replace("\n", " ").strip()[:24]
