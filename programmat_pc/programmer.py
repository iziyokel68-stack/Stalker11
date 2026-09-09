"""
☢ STALKER — ЕДИНЫЙ ПРОГРАММАТОР v1.0
=====================================
Поддерживает:
  • ЧИП       — предметы (аптечки, броня, артефакты, админка)
  • АНОМАЛИЯ  — настройка урона, режима, целей
  • УБЕЖИЩЕ   — настройка регена, очистки, пси-щита
  • ПДА       — (зарезервировано, будущее)

Протокол авто-определения устройства по Serial:
  PC  → "STALKER_WHO\n"
  ESP → "STALKER:CHIP_BOX:v1\n" | "STALKER:ANOMALY:v1\n" | ...
"""

import argparse
import json
import os
import sys
import time

from eeprom_txn import (
    TXN_STATE_IDLE, TXN_STATE_PENDING, TXN_STATE_PROCESSING,
    TXN_STATE_SUCCESS, TXN_STATE_FAILED,
    TXN_RESULT_OK, TXN_RESULT_INSUFFICIENT_FUNDS, TXN_RESULT_LEVEL_TOO_LOW,
    TXN_RESULT_SYSTEM_LOCKED, TXN_RESULT_BAD_CRC, TXN_RESULT_BAD_MAGIC,
)
from serial_link import (
    SerialLink, SERIAL_AVAILABLE, DEVICE_TYPE_MAP, list_port_names,
)

TXN_STATE_NAMES = {
    TXN_STATE_IDLE: "IDLE",
    TXN_STATE_PENDING: "PENDING",
    TXN_STATE_PROCESSING: "PROCESSING",
    TXN_STATE_SUCCESS: "SUCCESS",
    TXN_STATE_FAILED: "FAILED",
}
TXN_RESULT_NAMES = {
    TXN_RESULT_OK: "OK",
    TXN_RESULT_INSUFFICIENT_FUNDS: "INSUFFICIENT_FUNDS",
    TXN_RESULT_LEVEL_TOO_LOW: "LEVEL_TOO_LOW",
    TXN_RESULT_SYSTEM_LOCKED: "SYSTEM_LOCKED",
    TXN_RESULT_BAD_CRC: "BAD_CRC",
    TXN_RESULT_BAD_MAGIC: "BAD_MAGIC",
}

def parse_txn_kv(line: str) -> dict:
    """Разобрать TXN:state=1,txn_id=2,... в dict."""
    out = {}
    if not line:
        return out
    payload = line
    if payload.startswith("TXN:"):
        payload = payload[4:]
    for token in payload.split(","):
        if "=" in token:
            k, v = token.split("=", 1)
            try:
                out[k.strip()] = int(v.strip())
            except ValueError:
                out[k.strip()] = v.strip()
    return out

def format_txn_fields(fields: dict) -> str:
    st = fields.get("state", -1)
    st_name = TXN_STATE_NAMES.get(st, str(st))
    parts = [f"state={st_name}"]
    for key in ("txn_id", "amount", "item", "paid", "balance"):
        if key in fields:
            parts.append(f"{key}={fields[key]}")
    if "result" in fields:
        rc = fields["result"]
        rc_name = TXN_RESULT_NAMES.get(rc, str(rc))
        parts.append(f"result={rc_name}")
    return ", ".join(parts)

def format_txn_response(line: str) -> tuple:
    """Вернуть (ok: bool, краткое_сообщение, fields_dict)."""
    if not line:
        return False, "Нет ответа от устройства", {}
    if line.startswith("OK:PURCHASE_DONE:") or line.startswith("OK:TXN_DONE:"):
        fields = parse_txn_kv(line.split(":", 2)[2])
        return True, "Операция выполнена — " + format_txn_fields(fields), fields
    if line.startswith("OK:TXN_PENDING:"):
        fields = parse_txn_kv(line.split(":", 2)[2])
        return True, "Запрос отправлен — " + format_txn_fields(fields), fields
    if line.startswith("OK:TXN_IDLE"):
        return True, "Транзакция сброшена (IDLE)", {}
    if line.startswith("ERROR:PURCHASE_FAILED:") or line.startswith("ERROR:TXN_FAILED:"):
        fields = parse_txn_kv(line.split(":", 2)[2])
        return False, "Операция отклонена — " + format_txn_fields(fields), fields
    if line.startswith("ERROR:TXN_BUSY:"):
        fields = parse_txn_kv(line.split(":", 2)[2])
        return False, "Транзакция занята — " + format_txn_fields(fields), fields
    if line.startswith("TXN:"):
        fields = parse_txn_kv(line)
        return True, format_txn_fields(fields), fields
    if line.startswith("ERROR:"):
        err = line[6:].split(":", 1)[0]
        labels = {
            "TIMEOUT": "Таймаут ожидания PDA",
            "BAD_AMOUNT": "Некорректная сумма",
            "EEPROM_NOT_FOUND": "EEPROM не найден",
            "WRITE_FAILED": "Ошибка записи EEPROM",
            "READ_FAILED": "Ошибка чтения EEPROM",
            "BAD_CRC": "CRC блока транзакции",
            "TXN_BUSY": "Транзакция занята",
            "RESET_FAILED": "Сброс не выполнен",
            "UNKNOWN_CMD": "Неизвестная команда",
        }
        return False, labels.get(err, line), {}
    return False, line, {}

TERMINAL_ROLES = ("STORE", "ATM", "QUEST", "ADMIT", "BANK")
TERMINAL_ROLE_LABELS = {
    "STORE": "Касса",
    "ATM": "Банкомат",
    "QUEST": "Квестовая доска",
    "ADMIT": "Допуск",
    "BANK": "Банк",
}

def is_txn_device(dev_type):
    return dev_type in ("TERMINAL", "CASHIER", "CHIP_BOX")

def has_txn_wait(dev_type):
    return dev_type in ("TERMINAL", "CASHIER")

def has_terminal_role(dev_type):
    return dev_type == "TERMINAL"

# SerialLink / DEVICE_TYPE_MAP — programmat_pc/serial_link.py
# (общий модуль для программатора и приложения мастера).

def run_txn_cli():
    parser = argparse.ArgumentParser(description="STALKER cashier EEPROM TXN CLI")
    parser.add_argument("--port", default="AUTO", help="COM port or AUTO")
    sub = parser.add_subparsers(dest="action", required=True)

    p_start = sub.add_parser("start", help="TXN_START purchase request")
    p_start.add_argument("--amount", type=int, required=True)
    p_start.add_argument("--item", type=int, default=0)
    p_start.add_argument("--txn-id", type=int, default=0)

    sub.add_parser("status", help="TXN_STATUS")
    p_wait = sub.add_parser("wait", help="TXN_WAIT (CASHIER only)")
    p_wait.add_argument("--timeout", type=int, default=30000, help="timeout_ms")
    sub.add_parser("reset", help="TXN_RESET")

    args = parser.parse_args(sys.argv[2:])
    if not SERIAL_AVAILABLE:
        print("ERROR: pyserial not installed", file=sys.stderr)
        sys.exit(2)

    link = SerialLink()
    if not link.connect_sync(args.port):
        print(f"ERROR: device not found on {args.port}", file=sys.stderr)
        sys.exit(1)
    if not is_txn_device(link.dev_type):
        print(f"ERROR: {link.dev_type} does not support TXN", file=sys.stderr)
        sys.exit(1)

    if args.action == "start":
        resp = link.txn_start(args.amount, args.item, args.txn_id)
    elif args.action == "status":
        resp = link.txn_status()
    elif args.action == "wait":
        if not has_txn_wait(link.dev_type):
            print("ERROR: TXN_WAIT not supported on CHIP_BOX", file=sys.stderr)
            sys.exit(1)
        resp = link.txn_wait(args.timeout)
    else:
        resp = link.txn_reset()

    ok, msg, _fields = format_txn_response(resp or "")
    print(resp or "")
    print(("OK: " if ok else "FAIL: ") + msg)
    sys.exit(0 if ok else 1)


if __name__ == "__main__" and len(sys.argv) > 1 and sys.argv[1] == "txn":
    run_txn_cli()

# ─────────────────────────────────────────────────────────────
#  ИНИЦИАЛИЗАЦИЯ
# ─────────────────────────────────────────────────────────────
import pygame

pygame.init()
WIN_W, WIN_H = 390, 780
screen = pygame.display.set_mode((WIN_W, WIN_H))
pygame.display.set_caption("STALKER -- PROGRAMMER v1.0")

# Шрифты — компактные под телефонный размер
try:
    F_LG = pygame.font.SysFont("Consolas", 16, bold=True)
    F_MD = pygame.font.SysFont("Consolas", 13)
    F_SM = pygame.font.SysFont("Consolas", 11)
    F_XS = pygame.font.SysFont("Consolas", 10)
except:
    F_LG = pygame.font.SysFont(None, 18, bold=True)
    F_MD = pygame.font.SysFont(None, 15)
    F_SM = pygame.font.SysFont(None, 13)
    F_XS = pygame.font.SysFont(None, 12)

# ─────────────────────────────────────────────────────────────
#  ЦВЕТОВАЯ СХЕМА
# ─────────────────────────────────────────────────────────────
# Каждый режим устройства имеет свой акцентный цвет
DEVICE_COLORS = {
    0: (255, 200,  80),   # ЧИП      — янтарный
    1: (255,  80,  80),   # АНОМАЛИЯ — красный
    2: ( 80, 220, 120),   # УБЕЖИЩЕ  — зелёный
    3: ( 80, 160, 255),   # ПДА      — синий
    4: (220, 180, 255),   # ТЕРМИНАЛ  — фиолетовый
}
DEVICE_TERMINAL = 4
C_BG        = (22, 22, 28)        # фон окна
C_PANEL     = (32, 34, 42)        # фон панели
C_LINE      = (60, 62, 75)        # разделители
C_TEXT      = (220, 220, 220)     # основной текст
C_DIM       = (120, 120, 140)     # второстепенный текст
C_WHITE     = (255, 255, 255)
C_BTN       = (50, 52, 65)        # кнопка обычная
C_BTN_HOV   = (70, 72, 90)        # кнопка hover
C_BTN_OK    = (40, 140, 60)       # кнопка "прошить"
C_BTN_OK_FLS= (200, 240, 200)     # мигание при успехе
C_BTN_ERR_FLS = (240, 80, 80)     # мигание при ошибке записи
C_BOX       = (40, 42, 55)        # поле ввода
C_BOX_ACT   = (60, 80, 130)       # активное поле ввода
C_HEADER_BG = (28, 28, 36)        # фон шапки

# ─────────────────────────────────────────────────────────────
#  ДАННЫЕ: устройства, типы, подтипы
# ─────────────────────────────────────────────────────────────
DEVICES = ["ЧИП", "АНОМАЛИЯ", "УБЕЖИЩЕ", "ПДА ⚙", "ТЕРМИНАЛ"]

# --- ЧИП ---
CHIP_TYPES = ["РАСХОДНИКИ", "БРОНЯ", "АРТЕФАКТЫ", "АДМИНКА"]
CHIP_SUBTYPES = {
    0: ["МГНОВЕННОЕ ЛЕЧЕНИЕ", "АНТИРАДИН", "РЕГЕНЕРАТОР",
        "СТИМУЛЯТОР", "ВОССТАНОВЛЕНИЕ", "УЛУЧШЕНИЕ"],
    1: ["УНИВЕРСАЛЬНЫЙ ЧИП"],
    2: ["УНИВЕРСАЛЬНЫЙ ЧИП"],
    3: ["ВОСКРЕШЕНИЕ (любой мастер)", "УПРАВЛЕНИЕ ДЕНЬГАМИ",
        "УРОВЕНЬ/ОПЫТ", "ИММУНИТЕТ", "СБРОС", "НЕЙТРАЛИЗАЦИЯ",
        "ДОПУСК В ИГРУ (главный мастер)"],
}
DMG_TYPES = [
    ("ВЗРЫВ",    1,  2),
    ("КРОВЬ",    3,  4),
    ("ТЕРМО",    5,  6),
    ("ЭЛЕКТРО",  7,  8),
    ("ХИМИЯ",    9, 10),
    ("РАДИО",   11, 12),
    ("ПСИ",     13, 14),
    ("ГРАВИ",   15, 16),
    ("РАДИАЦИЯ",17, 18),
]

# --- АНОМАЛИЯ ---
ANOM_SUBTYPES = [
    "ВЗРЫВ", "КРОВОТЕЧЕНИЕ", "ЖАРКА (Термо)", "ЭЛЕКТРА",
    "КИСЕЛЬ (Хим)", "РАДИАЦИЯ", "ПСИ-ПОЛЕ", "ГРАВИТАЦИЯ",
]
RECHARGE_MODES = ["РЕАКТИВНЫЙ (Засада)", "АВТОНОМНЫЙ (Гейзер)"]
TARGET_MODES   = ["ОДИН ИГРОК", "ПОДРЫВ (Мина)", "ОБЛАКО (Зона)"]

# Чекбоксы HP-типов (без РАДИАЦИИ — она отдельной строкой)
DMG_NAMES_CB  = ["ВЗРЫВ", "КРОВЬ", "ТЕРМО", "ЭЛЕКТРО", "ХИМИЯ", "ПСИ", "ГРАВИТ."]
DMG_COLORS_CB = [
    (255, 150,  50),   # 0 ВЗРЫВ  → dmg_mask bit 0
    (220,  60,  60),   # 1 КРОВЬ  → dmg_mask bit 1
    (255,  90,  40),   # 2 ТЕРМО  → dmg_mask bit 2
    (100, 180, 255),   # 3 ЭЛЕКТРО → dmg_mask bit 3
    (140, 220,  80),   # 4 ХИМИЯ  → dmg_mask bit 4
    (200,  80, 220),   # 5 ПСИ    → dmg_mask bit 6
    (120, 120, 220),   # 6 ГРАВИТ. → dmg_mask bit 7
]
RAD_COLOR     = (100, 220, 120)  # цвет строки РАДИАЦИЯ

# COM-порты: AUTO + живые порты Windows + запасной статический список (COM1..32)
def _com_sort_key(name):
    if name == "AUTO":
        return (-1, 0)
    if name.upper().startswith("COM"):
        try:
            return (0, int(name[3:]))
        except ValueError:
            pass
    return (1, name)

def get_com_ports():
    static = [f"COM{i}" for i in range(1, 33)]
    if not SERIAL_AVAILABLE:
        return ["AUTO"] + static
    live = sorted(set(list_port_names()), key=_com_sort_key)
    ports = ["AUTO"]
    for name in live + static:
        if name not in ports:
            ports.append(name)
    ports[1:] = sorted(ports[1:], key=_com_sort_key)
    return ports

serial_link = SerialLink()
# Автозапуск поиска при старте (только если pyserial есть)
if SERIAL_AVAILABLE:
    serial_link.scan("AUTO")

# ─────────────────────────────────────────────────────────────
#  СОСТОЯНИЕ
# ─────────────────────────────────────────────────────────────
CFG_FILE = os.path.join(os.path.dirname(__file__), "programmer_state.json")

state = {
    # Навигация
    "device_idx":   0,     # 0=Чип, 1=Аномалия, 2=Убежище, 3=ПДА, 4=Терминал
    "com_idx":      7,     # индекс COM-порта (7=AUTO)
    # Чип
    "chip_type":    0,     # 0=Расходники, 1=Броня, 2=Арты, 3=Админка
    "chip_sub":     0,
    "chip_uses":    1,
    "armor_interval": 30,   # интервал регена брони (сек)
    "heal_mode":      0,    # 0=в единицах HP, 1=в процентах MaxHP
    "rad_mode":       0,    # 0=в единицах RAD, 1=в процентах накопленного RAD
    "regen_hp_mode":  0,    # 0=HP/сек, 1=%MaxHP/сек (регенератор)
    "regen_rad_mode": 0,    # 0=RAD/сек, 1=%тек.RAD/сек (регенератор)
    "restore_mode":   0,    # 0=единицы, 1=% от макс. прочности (восстановление)
    **{f"s{i}": 0 for i in range(21)},  # s0-s7=защиты брони %, s8=реген HP/мин брони; s0=реген арта...
    # Аномалия
    "anom_sub":      0,
    "anom_dmg_mask": 1,   # bitmask: bit0-7=HP-типы (радиация отдельно — rad_dmg)
    "anom_dmg":      10,
    "anom_rad_dmg":  0,    # Начисление RAD за тик (val2, 0 = нет радиации)
    "anom_dmg_max": 20,
    "anom_dmg_step":1,
    "anom_freq":    60,    # сек: время перезарядки HP-удара
    "anom_recharge":0,
    "anom_target":  0,
    "anom_erupt":   30,
    "anom_hits":    3,
    "anom_radius":  5,
    "anom_rad_freq": 10,  # сек: интервал RAD-таймера
    "anom_rad_on":  False, # True = радиация включена
    # Убежище
    "sz_regen":      5,
    "sz_rad":        2,
    "sz_hp_freq":    30,    # сек: интервал регена HP
    "sz_rad_freq":   60,    # сек: интервал очистки RAD
    "sz_emission":   False, # True = защищает от ВЫБРОСА
    "sz_prot":       [0]*8, # [0-6]=% HP-типы, [7]=% RAD
    "sz_radius":     10,
    "terminal_role_idx": 0,
}

if os.path.exists(CFG_FILE):
    try:
        with open(CFG_FILE, "r") as f:
            state.update(json.load(f))
    except:
        pass
state["device_idx"] = state.get("device_idx", 0) % len(DEVICES)

def save_state():
    with open(CFG_FILE, "w") as f:
        json.dump(state, f, indent=2, ensure_ascii=False)

def is_terminal_page():
    return state.get("device_idx") == DEVICE_TERMINAL

def txn_ready():
    """Подключён TERMINAL / CASHIER / CHIP_BOX (фоновый scan, без TXN UI)."""
    return (SERIAL_AVAILABLE and serial_link.connected
            and is_txn_device(serial_link.dev_type))

def trigger_terminal_scan():
    """Поиск устройства на выбранном COM (или AUTO)."""
    if not SERIAL_AVAILABLE:
        return
    com_ports = get_com_ports()
    state["com_idx"] = min(state.get("com_idx", 0), len(com_ports) - 1)
    target = com_ports[state["com_idx"] % len(com_ports)]
    serial_link.scan("AUTO" if target == "AUTO" else target)

# (port, dev_type) для которого уже открыли страницу — не трогаем выбор пользователя.
_auto_page_for = None

def maybe_auto_switch_device():
    """Один раз при подключении открыть вкладку под тип ESP. Дальше — вручную."""
    global _auto_page_for
    if not SERIAL_AVAILABLE or not serial_link.connected:
        _auto_page_for = None
        return
    key = (serial_link.port, serial_link.dev_type)
    if key == _auto_page_for:
        return
    page = DEVICE_TYPE_MAP.get(serial_link.dev_type)
    if page is not None:
        state["device_idx"] = page
    _auto_page_for = key

def sync_terminal_role_from_link():
    """Синхронизировать индекс роли из подключённого TERMINAL (без лишних запросов)."""
    if not has_terminal_role(serial_link.dev_type):
        return
    role = serial_link.terminal_role
    if role and role in TERMINAL_ROLES:
        idx = TERMINAL_ROLES.index(role)
        if state.get("terminal_role_idx") != idx:
            state["terminal_role_idx"] = idx
            save_state()

def apply_terminal_role_idx(idx):
    state["terminal_role_idx"] = idx % len(TERMINAL_ROLES)
    save_state()

def flash_terminal_role():
    """Подключиться к COM и записать TERMINAL_ROLE (как ПРОШИТЬ для чипов)."""
    if not SERIAL_AVAILABLE:
        return True
    com_ports = get_com_ports()
    state["com_idx"] = min(state.get("com_idx", 0), len(com_ports) - 1)
    target = com_ports[state["com_idx"] % len(com_ports)]
    target = "AUTO" if target == "AUTO" else target
    if not serial_link.connected:
        if not serial_link.connect_sync(target):
            return False
    role = TERMINAL_ROLES[state.get("terminal_role_idx", 0) % len(TERMINAL_ROLES)]
    ok, _resp = serial_link.terminal_role_set(role)
    return ok

# ─────────────────────────────────────────────────────────────
#  ВИДЖЕТЫ
# ─────────────────────────────────────────────────────────────
class Button:
    def __init__(self, x, y, w, h, text, tag, accent=None):
        self.rect   = pygame.Rect(x, y, w, h)
        self.text   = text
        self.tag    = tag
        self.accent = accent   # если задан — используется как цвет

    def draw(self, surf, override=None):
        mx, my = pygame.mouse.get_pos()
        hov = self.rect.collidepoint(mx, my)
        if override:
            col = override
        elif self.accent:
            col = tuple(min(255, c + 30) for c in self.accent) if hov else self.accent
        else:
            col = C_BTN_HOV if hov else C_BTN
        pygame.draw.rect(surf, col, self.rect, border_radius=6)
        pygame.draw.rect(surf, C_LINE, self.rect, 1, border_radius=6)
        tx = F_MD.render(self.text, True, C_WHITE)
        surf.blit(tx, tx.get_rect(center=self.rect.center))

    def hit(self, pos):
        return self.rect.collidepoint(pos)


class NavRow:
    """Строка навигации: < [текст] >"""
    def __init__(self, x, y, w, h, tag_prev, tag_next):
        self.rect     = pygame.Rect(x, y, w, h)
        self.btn_prev = Button(x,         y, 36, h, "◀", tag_prev)
        self.btn_next = Button(x+w-36,    y, 36, h, "▶", tag_next)
        self.inner    = pygame.Rect(x+40, y, w-80, h)

    def draw(self, surf, text, color, bg=None):
        if bg:
            pygame.draw.rect(surf, bg, self.rect, border_radius=6)
        pygame.draw.rect(surf, C_LINE, self.rect, 1, border_radius=6)
        self.btn_prev.draw(surf)
        self.btn_next.draw(surf)
        tx = F_MD.render(text, True, color)
        surf.blit(tx, tx.get_rect(center=self.inner.center))

    def hits(self, pos):
        if self.btn_prev.hit(pos): return "prev"
        if self.btn_next.hit(pos): return "next"
        return None


class TextBox:
    def __init__(self, x, y, w, h, val, max_chars=5, allow_neg=False):
        self.rect      = pygame.Rect(x, y, w, h)
        self.text      = str(val)
        self.max_chars = max_chars
        self.allow_neg = allow_neg
        self.active    = False

    @property
    def value(self):
        try:    return int(self.text) if self.text not in ("", "-") else 0
        except: return 0

    def draw(self, surf):
        col_bg  = C_BOX_ACT if self.active else C_BOX
        col_brd = C_WHITE   if self.active else C_LINE
        pygame.draw.rect(surf, col_bg,  self.rect, border_radius=5)
        pygame.draw.rect(surf, col_brd, self.rect, 1, border_radius=5)
        tx = F_MD.render(self.text, True, C_WHITE if self.active else C_TEXT)
        surf.blit(tx, tx.get_rect(center=self.rect.center))

    def hit(self, pos):
        return self.rect.collidepoint(pos)

    def key(self, event):
        if event.key == pygame.K_BACKSPACE:
            self.text = self.text[:-1] or "0"
        elif event.unicode == "-" and self.allow_neg:
            self.text = self.text[1:] if self.text.startswith("-") else "-" + self.text
        elif event.unicode in "0123456789":
            if self.text in ("0", "-0"):
                self.text = ("-" if self.text == "-0" else "") + event.unicode
            elif len(self.text) < self.max_chars:
                self.text += event.unicode


# ─────────────────────────────────────────────────────────────
#  LAYOUT — позиции рядов
# ─────────────────────────────────────────────────────────────
PAD   = 10      # горизонтальный отступ от краёв
ROW_H = 32      # высота строки
ROW_G = 5       # зазор между строками
INNER_W = WIN_W - PAD * 2

def row_y(n):
    """Y-координата N-го ряда (0-based), начиная после шапки."""
    TOP = 82   # высота шапки
    return TOP + n * (ROW_H + ROW_G)

# --- Навигационные ряды (фиксированы) ---
NAV_DEVICE = NavRow(PAD, row_y(0), INNER_W, ROW_H, "dev_prev", "dev_next")
NAV_TYPE   = NavRow(PAD, row_y(1), INNER_W, ROW_H, "typ_prev", "typ_next")
NAV_SUBTYPE= NavRow(PAD, row_y(2), INNER_W, ROW_H, "sub_prev", "sub_next")
NAV_TERMINAL_ROLE = NavRow(PAD, row_y(1), INNER_W, ROW_H, "trole_prev", "trole_next")
SEP_Y      = row_y(3) - 3   # горизонтальный разделитель перед параметрами

# --- COM-порт и кнопка Flash (снизу, фиксированы) ---
NAV_COM    = NavRow(PAD, WIN_H - 88, INNER_W, ROW_H, "com_prev", "com_next")
BTN_FLASH  = Button(PAD + INNER_W//4, WIN_H - 50, INNER_W//2, 38,
                    "▶  ПРОШИТЬ", "flash", accent=(40, 130, 55))

# ─────────────────────────────────────────────────────────────
#  ПОЛЯ ВВОДА — создаются один раз, переиспользуются
# ─────────────────────────────────────────────────────────────
PARAMS_X  = PAD
LABEL_W   = 192
BOX_W     = 58
BOX_X     = PAD + LABEL_W + 4
UNIT_X    = BOX_X + BOX_W + 5

def make_box(row, key, allow_neg=False):
    y = row_y(row)
    return TextBox(BOX_X, y, BOX_W, ROW_H, state.get(key, 0),
                   max_chars=6, allow_neg=allow_neg)

# Чип-поля
bx_s = [TextBox(BOX_X, 0, BOX_W, ROW_H, state.get(f"s{i}", 0),
                max_chars=6, allow_neg=True) for i in range(21)]
bx_uses = TextBox(BOX_X, 0, BOX_W, ROW_H, state.get("chip_uses", 1))

# Артефакт — таймеры урона по каждому типу (8 типов × ЧЧ:ММ:СС)
_art_frq = [state.get(f"art_frq_{i}", 60) for i in range(8)]
bx_art_hh = [TextBox(0, 0, 24, ROW_H, f"{_art_frq[i]//3600:02d}", max_chars=2) for i in range(8)]
bx_art_mm = [TextBox(0, 0, 24, ROW_H, f"{(_art_frq[i]%3600)//60:02d}", max_chars=2) for i in range(8)]
bx_art_ss = [TextBox(0, 0, 24, ROW_H, f"{_art_frq[i]%60:02d}", max_chars=2) for i in range(8)]

# Артефакт — интервал регена HP (ЧЧ:ММ:СС)
_art_int = state.get("art_interval", 30)  # сек, по умолчанию 30с
bx_art_int_hh = TextBox(BOX_X,       0, 44, ROW_H, f"{_art_int//3600:02d}",            max_chars=2)
bx_art_int_mm = TextBox(BOX_X + 52,  0, 44, ROW_H, f"{(_art_int%3600)//60:02d}",       max_chars=2)
bx_art_int_ss = TextBox(BOX_X + 104, 0, 44, ROW_H, f"{_art_int%60:02d}",               max_chars=2)

# Броня — интервал регена HP (ЧЧ:ММ:СС)
_arm_int = state.get("armor_interval", 30)
bx_arm_int_hh = TextBox(BOX_X,       0, 44, ROW_H, f"{_arm_int//3600:02d}",            max_chars=2)
bx_arm_int_mm = TextBox(BOX_X + 52,  0, 44, ROW_H, f"{(_arm_int%3600)//60:02d}",       max_chars=2)
bx_arm_int_ss = TextBox(BOX_X + 104, 0, 44, ROW_H, f"{_arm_int%60:02d}",               max_chars=2)

# Аномалия-поля
bx_a_dmg     = TextBox(BOX_X, 0, BOX_W, ROW_H, state.get("anom_dmg", 10),      allow_neg=True)
bx_a_rad_dmg = TextBox(BOX_X, 0, BOX_W, ROW_H, state.get("anom_rad_dmg", 0),   allow_neg=True)
bx_a_dmax    = TextBox(BOX_X, 0, BOX_W, ROW_H, state.get("anom_dmg_max", 20),  allow_neg=True)
bx_a_dstep   = TextBox(BOX_X, 0, BOX_W, ROW_H, state.get("anom_dmg_step", 1),  allow_neg=True)
bx_a_h     = TextBox(BOX_X,       0, 44, ROW_H, f"{state.get('anom_freq',60)//3600:02d}", max_chars=2)
bx_a_m     = TextBox(BOX_X + 52,  0, 44, ROW_H, f"{(state.get('anom_freq',60)%3600)//60:02d}", max_chars=2)
bx_a_s2    = TextBox(BOX_X + 104, 0, 44, ROW_H, f"{state.get('anom_freq',60)%60:02d}", max_chars=2)
bx_a_erupt = TextBox(BOX_X, 0, BOX_W, ROW_H, state.get("anom_erupt", 30))
bx_a_hits  = TextBox(BOX_X, 0, BOX_W, ROW_H, state.get("anom_hits", 3))
bx_a_rad   = TextBox(BOX_X, 0, BOX_W, ROW_H, state.get("anom_radius", 5))
# RAD-таймер (ЧЧ:ММ:СС)
_rf = state.get("anom_rad_freq", 10)
bx_a_rh = TextBox(BOX_X,       0, 44, ROW_H, f"{_rf//3600:02d}", max_chars=2)
bx_a_rm = TextBox(BOX_X + 52,  0, 44, ROW_H, f"{(_rf%3600)//60:02d}", max_chars=2)
bx_a_rs = TextBox(BOX_X + 104, 0, 44, ROW_H, f"{_rf%60:02d}", max_chars=2)
NAV_RECHARGE  = NavRow(PAD, 0, INNER_W, ROW_H, "rm_prev", "rm_next")
NAV_TARGET    = NavRow(PAD, 0, INNER_W, ROW_H, "tg_prev", "tg_next")
NAV_HEAL_MODE = NavRow(PAD, 0, INNER_W, ROW_H, "hm_prev", "hm_next")  # HP/% переключатель
NAV_REGEN_MODE= NavRow(PAD, 0, INNER_W, ROW_H, "rgm_prev", "rgm_next") # RAD/% регенератор

# Убежище-поля
_szh = state.get("sz_hp_freq", 30)
_szr = state.get("sz_rad_freq", 60)
bx_sz_regen = TextBox(BOX_X, 0, BOX_W, ROW_H, state.get("sz_regen", 5),   allow_neg=True)
bx_sz_rad   = TextBox(BOX_X, 0, BOX_W, ROW_H, state.get("sz_rad", 2),     allow_neg=True)
bx_sz_rds   = TextBox(BOX_X, 0, BOX_W, ROW_H, state.get("sz_radius", 10))
# Таймер регена HP (ЧЧ:ММ:СС)
bx_sz_hh = TextBox(BOX_X,       0, 44, ROW_H, f"{_szh//3600:02d}", max_chars=2)
bx_sz_hm = TextBox(BOX_X + 52,  0, 44, ROW_H, f"{(_szh%3600)//60:02d}", max_chars=2)
bx_sz_hs = TextBox(BOX_X + 104, 0, 44, ROW_H, f"{_szh%60:02d}", max_chars=2)
# Таймер очистки RAD (ЧЧ:ММ:СС)
bx_sz_rh = TextBox(BOX_X,       0, 44, ROW_H, f"{_szr//3600:02d}", max_chars=2)
bx_sz_rm = TextBox(BOX_X + 52,  0, 44, ROW_H, f"{(_szr%3600)//60:02d}", max_chars=2)
bx_sz_rs = TextBox(BOX_X + 104, 0, 44, ROW_H, f"{_szr%60:02d}", max_chars=2)
# Поля % защит убежища: индекс 0-6 = HP-типы, 7 = RAD (индивидуальные значения)
_szp = state.get("sz_prot", [0]*8)
if len(_szp) < 8: _szp = (_szp + [0]*8)[:8]
bx_sz_prot = [TextBox(0, 0, 40, ROW_H, _szp[i], max_chars=3, allow_neg=True) for i in range(8)]

# ПДА КОНФИГ поля
NAV_PDA_MODE = NavRow(PAD, 0, INNER_W, ROW_H, "pda_prev", "pda_next")  # ФУНКЦИИ/ПРЕСЕТ
bx_pda_maxhp    = TextBox(BOX_X, 0, BOX_W, ROW_H, state.get("pda_maxhp", 1000))
bx_pda_starthp  = TextBox(BOX_X, 0, BOX_W, ROW_H, state.get("pda_starthp", 1000))
bx_pda_maxrad   = TextBox(BOX_X, 0, BOX_W, ROW_H, state.get("pda_maxrad", 1000))
bx_pda_money    = TextBox(BOX_X, 0, BOX_W, ROW_H, state.get("pda_money", 1000))
bx_pda_level    = TextBox(BOX_X, 0, BOX_W, ROW_H, state.get("pda_level", 1))
bx_pda_xp      = TextBox(BOX_X, 0, BOX_W, ROW_H, state.get("pda_xp", 0))
_pdarots = state.get("pda_base_prot", [0]*8)
if len(_pdarots) < 8: _pdarots = (_pdarots + [0]*8)[:8]
bx_pda_prot = [TextBox(0, 0, 40, ROW_H, _pdarots[i], max_chars=3) for i in range(8)]

# ─────────────────────────────────────────────────────────────
#  ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ РЕНДЕРА
# ─────────────────────────────────────────────────────────────
def draw_row_label(surf, y, label, unit="", accent=C_TEXT):
    surf.blit(F_MD.render(label, True, accent), (PARAMS_X, y + (ROW_H - 14) // 2))
    if unit:
        surf.blit(F_SM.render(unit, True, C_DIM), (UNIT_X, y + (ROW_H - 12) // 2))

def draw_sep(surf, y, label=""):
    pygame.draw.line(surf, C_LINE, (PAD, y), (WIN_W - PAD, y))
    if label:
        lbl = F_XS.render(f" {label} ", True, C_DIM)
        surf.blit(lbl, (PAD + 8, y - 8))

def place(box, y):
    """Установить Y-позицию поля ввода."""
    box.rect.y = y

# ─────────────────────────────────────────────────────────────
#  ПАРАМЕТРЫ ДЛЯ КАЖДОГО РЕЖИМА
# ─────────────────────────────────────────────────────────────
# Метки и цвета защит для чипов (броня / артефакты)
CHIP_PROT_LABELS = ["ВЗРЫВ", "КРОВЬ", "ТЕРМО", "ЭЛЕКТРО", "ХИМИЯ", "ПСИ", "ГРАВИТ.", "RAD"]
CHIP_PROT_COLORS = [
    (255, 150,  50),  # взрыв
    (220,  60,  60),  # кровь
    (255,  90,  40),  # термо
    (100, 180, 255),  # электро
    (140, 220,  80),  # химия
    (200,  80, 220),  # пси
    (120, 120, 220),  # гравит.
    (100, 220, 120),  # RAD
]

def chip_params_fields():
    """Возвращает список (label, unit, box) для текущего чип-режима."""
    t  = state["chip_type"]
    st = state["chip_sub"]
    rows = []

    if t == 3:  # АДМИНКА
        if st == 0:  # ВОСКРЕШЕНИЕ — без параметров
            pass
        elif st == 6:  # ДОПУСК В ИГРУ (главный мастер)
            rows.append(("ЧЕРЕЗ (ЧАСЫ):",  "ЧЧ",  bx_s[0]))
            rows.append(("ЧЕРЕЗ (МИНУТЫ):", "ММ",  bx_s[1]))
        elif st == 1:  # ДЕНЬГИ
            rows.append(("СУММА (+/-):",       "РУБ", bx_s[0]))
        elif st == 2:  # УРОВЕНЬ/ОПЫТ
            rows.append(("УРОВЕНЬ (+/-):",    "LVL", bx_s[0]))
            rows.append(("ОПЫТ (+/-):",       "XP",  bx_s[1]))
        elif st == 3:  # ИММУНИТЕТ: логика как стимулятор, но без защит
            rows.append(("ДЛИТЕЛЬНОСТЬ:",  "МИН", bx_s[0]))
        # st==4 СБРОС и st==5 НЕЙТРАЛИЗАЦИЯ: без полей, рендерятся вручную
        # bx_uses — рисуется фиксированно внизу

    elif t == 0:  # РАСХОДНИКИ
        hm = state.get("heal_mode", 0)
        heal_unit = "HP" if hm == 0 else "% MaxHP"
        rm = state.get("rad_mode", 0)
        rad_unit  = "RAD" if rm == 0 else "% тек.RAD"  # поясняем что % от текущего
        if st == 0:   rows.append(("ВЫЛЕЧИТЬ:",       heal_unit, bx_s[0]))
        elif st == 1: rows.append(("СНЯТЬ РАДИАЦИЮ:", rad_unit,  bx_s[0]))
        elif st == 2:  # РЕГЕНЕРАТОР — HP и RAD каналы
            rhm = state.get("regen_hp_mode",  0)
            rrm = state.get("regen_rad_mode", 0)
            hp_unit  = "HP/СЕК"  if rhm == 0 else "% MaxHP/СЕК"
            rad_unit2= "RAD/СЕК" if rrm == 0 else "% тек.RAD/СЕК"
            rows.append(("РЕГЕН HP:",   hp_unit,  bx_s[0]))
            rows.append(("ДЛИТ. HP:",   "СЕК",    bx_s[1]))
            rows.append(("РЕГЕН RAD:",  rad_unit2, bx_s[2]))
            rows.append(("ДЛИТ. RAD:",  "СЕК",    bx_s[3]))
        elif st == 3:  # СТИМУЛЯТОР: грид защит рендерится вручную, здесь только длительность
            rows.append(("ДЛИТЕЛЬНОСТЬ:", "СЕК", bx_s[8]))
        elif st == 4:  # ВОССТАНОВЛЕНИЕ: кап min(maxUses, cur+amt)
            rm2 = state.get("restore_mode", 0)
            rst_unit = "ЕД." if rm2 == 0 else "% макс."
            rows.append(("КОЛИЧЕСТВО:", rst_unit, bx_s[0]))
        elif st == 5:  # УЛУЧШЕНИЕ: постоянный апгрейд всех параметров предмета
            rows.append(("ДОП. ПРОЧН.:", "ЕД. (макс 250)", bx_s[0]))
            rows.append(("УЛУЧШ. ПАРАМ.:", "%",              bx_s[1]))
        # bx_uses — рисуется фиксированно внизу

    elif t == 1:  # БРОНЯ — реген HP + сетка 8 защит
        rows.append(("РЕГЕН HP/МИН:", "HP/мин", bx_s[8]))
        return rows, True   # True = рендерить грид защит (armor mode)
        # bx_uses — рисуется фиксированно внизу

    elif t == 2:  # АРТЕФАКТЫ — рег HP + интервал ЧЧ:ММ:СС (защиты/урон рендерятся отдельно)
        rows.append(("РЕГЕН HP/МИН:", "HP/мин", bx_s[0]))
        # ИНТЕРВАЛ — рендерится вручную через bx_art_int_hh/mm/ss
        # bx_uses — рисуется фиксированно внизу
        return rows, True   # True = рисовать двойную таблицу (защита | урон)
    return rows, False

def get_all_active_boxes():
    """Все TextBox, активные в текущем режиме."""
    if is_terminal_page():
        return []
    dev = state["device_idx"]
    if dev == 0:
        rows, has_art = chip_params_fields()
        boxes = [r[2] for r in rows]
        if has_art and state["chip_type"] == 1:  # БРОНЯ
            boxes += [bx_arm_int_hh, bx_arm_int_mm, bx_arm_int_ss]
            boxes += [bx_s[i] for i in range(8)]   # защиты bx_s[0]..bx_s[7]
            boxes.append(bx_s[9])                   # ДОП. HP
        elif has_art and state["chip_type"] == 2:  # АРТЕФАКТ
            # Интервал регена
            boxes += [bx_art_int_hh, bx_art_int_mm, bx_art_int_ss]
            # Защита % (bx_s[1..8]) + Урон HP (bx_s[9..16]) + таймеры урона
            for j in range(8):
                boxes.append(bx_s[j + 1])
                boxes.append(bx_s[j + 9])
                boxes += [bx_art_hh[j], bx_art_mm[j], bx_art_ss[j]]
        boxes.append(bx_uses)  # ПРОЧНОСТЬ — всегда активна
        # СТИМУЛЯТОР: добавить боксы защит bx_s[0..7]
        if state["device_idx"] == 0 and state["chip_type"] == 0 and state["chip_sub"] == 3:
            boxes += [bx_s[i] for i in range(8)]
        return boxes

    elif dev == 1:
        boxes = [bx_a_dmg, bx_a_dmax, bx_a_dstep,
                 bx_a_h, bx_a_m, bx_a_s2, bx_a_erupt, bx_a_hits, bx_a_rad]
        if state.get("anom_rad_on", False):
            boxes.insert(1, bx_a_rad_dmg)
            boxes += [bx_a_rh, bx_a_rm, bx_a_rs]
        return boxes
    elif dev == 2:
        return [bx_sz_regen, bx_sz_hh, bx_sz_hm, bx_sz_hs,
                bx_sz_rad,   bx_sz_rh, bx_sz_rm, bx_sz_rs,
                bx_sz_rds] + bx_sz_prot
    elif dev == 3:
        if state.get("pda_mode", 0) == 1:  # ПРЕСЕТ
            return [bx_pda_maxhp, bx_pda_starthp, bx_pda_maxrad,
                    bx_pda_money, bx_pda_level, bx_pda_xp] + bx_pda_prot
        return []  # ФУНКЦИИ — чекбоксы, не TextBox
    return []

# ─────────────────────────────────────────────────────────────
#  СОХРАНЕНИЕ CFG ПЕРЕД ПРОШИВКОЙ
# ─────────────────────────────────────────────────────────────
def collect_state():
    dev = state["device_idx"]
    if dev == 0:
        state["chip_uses"] = bx_uses.value
        for i, bx in enumerate(bx_s):
            state[f"s{i}"] = bx.value
        if state["chip_type"] == 1:  # сохраняем интервал регена брони
            state["armor_interval"] = (bx_arm_int_hh.value * 3600 +
                                       bx_arm_int_mm.value * 60 +
                                       bx_arm_int_ss.value)
        state["chip_type_saved"] = state["chip_type"]
        state["chip_sub_saved"]  = state["chip_sub"]
    elif dev == 1:
        state["anom_dmg"]      = bx_a_dmg.value
        state["anom_rad_dmg"]  = bx_a_rad_dmg.value
        state["anom_dmg_max"]  = bx_a_dmax.value
        state["anom_dmg_step"] = bx_a_dstep.value
        state["anom_freq"]     = bx_a_h.value*3600 + bx_a_m.value*60 + bx_a_s2.value
        state["anom_erupt"]    = bx_a_erupt.value
        state["anom_hits"]     = bx_a_hits.value
        state["anom_radius"]   = bx_a_rad.value
        state["anom_rad_freq"] = bx_a_rh.value*3600 + bx_a_rm.value*60 + bx_a_rs.value
        # anom_rad_on сохраняется через клик чекбокса напрямую в state
    elif dev == 2:
        state["sz_regen"]    = bx_sz_regen.value
        state["sz_rad"]      = bx_sz_rad.value
        state["sz_radius"]   = bx_sz_rds.value
        state["sz_hp_freq"]  = bx_sz_hh.value*3600 + bx_sz_hm.value*60 + bx_sz_hs.value
        state["sz_rad_freq"] = bx_sz_rh.value*3600 + bx_sz_rm.value*60 + bx_sz_rs.value
        state["sz_prot"]     = [bx.value for bx in bx_sz_prot]   # 8 значений 0-100
        # sz_emission сохраняется через клик чекбокса
    save_state()

def build_config_str():
    """Собрать строку CONFIG_WRITE для текущего устройства."""
    dev = state["device_idx"]
    if dev == 0:  # ЧИП
        t  = state["chip_type"]
        st = state["chip_sub"]
        u  = bx_uses.value
        if t == 1:  # БРОНЯ: p0..p7=защиты%, p8=HP/тик, p9=интервал(сек)
            arm_int_sec = (bx_arm_int_hh.value * 3600 +
                           bx_arm_int_mm.value * 60 +
                           bx_arm_int_ss.value)
            hp_per_tick = round(bx_s[8].value * arm_int_sec / 60) if arm_int_sec > 0 else bx_s[8].value
            p = [bx_s[i].value for i in range(8)] + [hp_per_tick, arm_int_sec, bx_s[9].value] + [0]*5
            # p0-p7=защиты%, p8=HP/тик, p9=интервал, p10=ДОП.HP
            parts = ",".join(f"p{i}={p[i]}" for i in range(16))
            return f"type=1,sub=0,uses={u},{parts}"
        elif t == 2:  # АРТЕФАКТЫ: расширенный формат p0..p23
            # p0  = HP/тик (конверсия из HP/мин)
            # p1  = интервал регена (сек)
            # p2..p8  = защита % по 7 HP-типам (bx_s[1..7])
            # p9  = защита RAD % (bx_s[8])
            # p10..p16 = урон HP по 7 типам (bx_s[9..15])
            # p17 = RAD_ACCUM — накопление радиации за тик (bx_s[16])
            # p18..p23 = интервалы урона по 6 HP-типам (сек) [RAD-интервал → из bx_art_hh/mm/ss[7], но нет слота → будет = p18..p23[6] не влезает, RAD_INT хранить в отдельном бите]
            art_int_sec = bx_art_int_hh.value*3600 + bx_art_int_mm.value*60 + bx_art_int_ss.value
            hp_per_tick = round(bx_s[0].value * art_int_sec / 60) if art_int_sec > 0 else bx_s[0].value
            # Per-type интервалы (сек) — 8 штук
            art_dmg_int = []
            for j in range(8):
                art_dmg_int.append(
                    bx_art_hh[j].value * 3600 + bx_art_mm[j].value * 60 + bx_art_ss[j].value
                )
            p = ([hp_per_tick, art_int_sec] +         # p0, p1
                 [bx_s[i+1].value for i in range(7)]+ # p2..p8 = защиты 7 HP-типов
                 [bx_s[8].value] +                     # p9 = защита RAD
                 [bx_s[i+9].value for i in range(7)] + # p10..p16 = урон 7 HP-типов
                 [bx_s[16].value] +                     # p17 = RAD_DMG
                 art_dmg_int[:6])                        # p18..p23 = интервалы 6 HP-типов
            # RAD-интервал = art_dmg_int[7], но слотов ровно 24 — не влезает.
            # Решение: RAD-интервал = art_dmg_int[6] (ГРАВИТ) по умолчанию, или расширим до p24
            # Пока: дописываем RAD-интервал как p24 (25-й параметр)
            p.append(art_dmg_int[6])  # p24 = интервал ГРАВИТ
            p.append(art_dmg_int[7])  # p25 = интервал RAD
            parts = ",".join(f"p{i}={p[i]}" for i in range(len(p)))
            return f"type=2,sub=0,uses={u},{parts}"
        elif t == 3:  # АДМИНКА
            if st == 0:    p = [0]*16                                    # ВОСКРЕШЕНИЕ
            elif st == 6:  p = [bx_s[0].value, bx_s[1].value] + [0]*14  # ДОПУСК В ИГРУ: p0=часы, p1=мин
            elif st == 1:  p = [bx_s[0].value] + [0]*15                  # p0=РУБ
            elif st == 2:  p = [bx_s[0].value, bx_s[1].value] + [0]*14  # p0=уровень, p1=XP
            elif st == 3:  p = [bx_s[0].value] + [0]*15                  # p0=длин. мин
            else:          p = [0]*16                                     # СБРОС/НЕЙТРАЛ: команда без парам
            parts = ",".join(f"p{i}={p[i]}" for i in range(16))
            return f"type=3,sub={st},uses={u},{parts}"
        else:  # РАСХОДНИКИ
            if st == 2:  # РЕГЕНЕРАТОР: p0=HP/тик, p1=HP_mode, p2=длит.HP, p3=RAD/тик, p4=RAD_mode, p5=длит.RAD
                rhm = state.get("regen_hp_mode",  0)
                rrm = state.get("regen_rad_mode", 0)
                p = [bx_s[0].value, rhm, bx_s[1].value,
                     bx_s[2].value, rrm, bx_s[3].value] + [0]*10
            elif st == 3:  # СТИМУЛЯТОР: p0-p7=защиты%, p8=длительность(сек)
                p = [bx_s[i].value for i in range(8)] + [bx_s[8].value] + [0]*7
            else:  # st 0,1,4 — простые поля
                p = [bx_s[i].value for i in range(6)] + [0]*10
                if st == 0: p[1] = state.get("heal_mode", 0)   # p1=режим (0=HP, 1=%MaxHP)
                if st == 1: p[1] = state.get("rad_mode",  0)   # p1=режим (0=RAD, 1=%тек.RAD)
                if st == 4: p[1] = state.get("restore_mode", 0)  # p1=режим (0=ед., 1=%макс.)
                # st==5 УЛУЧШЕНИЕ: p0=доп.прочность bx_s[0], p1=% защит bx_s[1] — уже в p[0..1] автоматически
            parts = ",".join(f"p{i}={p[i]}" for i in range(16))
            return f"type=0,sub={st},uses={u},{parts}"
    # ОСТАЛЬНЫЕ устройства ниже:
    if dev == 1:  # АНОМАЛИЯ
        freq_ms = (bx_a_h.value*3600 + bx_a_m.value*60 + bx_a_s2.value) * 1000
        # sub_idx = первый выбранный тип (для совместимости с прошивкой)
        mask = state.get("anom_dmg_mask", 1)
        first_bit = next((i for i in range(8) if mask & (1 << i)), 0)
        return (
            f"cat=0,"
            f"sub={first_bit},"
            f"dmg_mask={mask},"
            f"dmg={bx_a_dmg.value},"
            f"dmg_max={bx_a_dmax.value},"
            f"dmg_stp={bx_a_dstep.value},"
            f"freq={bx_a_h.value*3600 + bx_a_m.value*60 + bx_a_s2.value},"
            f"rad_dmg={bx_a_rad_dmg.value if state.get('anom_rad_on') else 0},"
            f"rad_frq={bx_a_rh.value*3600 + bx_a_rm.value*60 + bx_a_rs.value if state.get('anom_rad_on') else 0},"
            f"rech={state['anom_recharge']},"
            f"tgt={state['anom_target']},"
            f"erupt={bx_a_erupt.value},"
            f"hits={bx_a_hits.value},"
            f"rad={bx_a_rad.value}"
        )
    elif dev == 2:
        hp_freq  = bx_sz_hh.value*3600 + bx_sz_hm.value*60 + bx_sz_hs.value
        rad_freq = bx_sz_rh.value*3600 + bx_sz_rm.value*60 + bx_sz_rs.value
        prot     = state.get("sz_prot", [0]*8)
        if len(prot) < 8: prot = (prot + [0]*8)[:8]
        prot_parts = ",".join(f"sz_p{i}={max(0,min(100,int(p)))}" for i,p in enumerate(prot))
        hp_per_tick = round(bx_sz_regen.value * hp_freq / 60) if hp_freq > 0 else bx_sz_regen.value
        return (
            f"cat=1,"
            f"sz_hp={hp_per_tick},"
            f"sz_hp_frq={hp_freq},"
            f"sz_rad={bx_sz_rad.value},"
            f"sz_rad_frq={rad_freq},"
            f"sz_emission={1 if state.get('sz_emission') else 0},"
            f"{prot_parts},"
            f"rad={bx_sz_rds.value}"
        )
    elif dev == 3:  # ПДА КОНФИГ
        mode = state.get("pda_mode", 0)
        if mode == 0:  # ФУНКЦИИ: битмаск
            flags = state.get("pda_func_flags", 0xFF)  # все включены по умолчанию
            return f"CONFIG:FUNC:flags={flags}"
        else:  # ПРЕСЕТ: начальные данные
            # Имя сталкера пишет модуль Регистрация (stalker_app): CONFIG:REGISTER:name=…
            bp = state.get("pda_base_prot", [0]*8)
            if len(bp) < 8: bp = (bp + [0]*8)[:8]
            prot_s = ",".join(f"r{i}={max(0,min(100,int(bp[i])))}" for i in range(8))
            return (
                f"CONFIG:PRESET:"
                f"maxhp={bx_pda_maxhp.value},"
                f"starthp={bx_pda_starthp.value},"
                f"maxrad={bx_pda_maxrad.value},"
                f"money={bx_pda_money.value},"
                f"lvl={bx_pda_level.value},"
                f"xp={bx_pda_xp.value},"
                f"{prot_s}"
            )
    return None

# ─────────────────────────────────────────────────────────────
#  ГЛАВНЫЙ ЦИКЛ
# ─────────────────────────────────────────────────────────────
clock = pygame.time.Clock()
flash_time = 0.0      # время последнего нажатия "ПРОШИТЬ"
flash_ok   = False    # True = успешная запись
flash_err  = ""       # текст ошибки, если запись не ушла на ESP

running = True
while running:
    now = time.time()
    maybe_auto_switch_device()
    dev = state["device_idx"]
    com_ports = get_com_ports()
    state["com_idx"] = min(state.get("com_idx", 0), len(com_ports) - 1)
    accent = DEVICE_COLORS.get(dev, C_TEXT)
    mx, my = pygame.mouse.get_pos()
    active_boxes = get_all_active_boxes()

    # ── СОБЫТИЯ ──────────────────────────────────────────────
    for event in pygame.event.get():
        if event.type == pygame.QUIT:
            running = False

        if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
            pos = event.pos

            # Снять активацию со всех полей
            for bx in active_boxes:
                bx.active = bx.hit(pos)

            # Навигация — УСТРОЙСТВО
            hit = NAV_DEVICE.hits(pos)
            if hit:
                d = 1 if hit == "next" else -1
                new_dev = (dev + d) % len(DEVICES)
                if new_dev != dev:
                    state["device_idx"] = new_dev
                    state["chip_sub"] = state["anom_sub"] = 0
                    save_state()
                    if new_dev == DEVICE_TERMINAL and not serial_link.connected:
                        trigger_terminal_scan()

            # Терминал — роль устройства (страница Терминал)
            if is_terminal_page():
                hit = NAV_TERMINAL_ROLE.hits(pos)
                if hit:
                    d = 1 if hit == "next" else -1
                    idx = (state.get("terminal_role_idx", 0) + d) % len(TERMINAL_ROLES)
                    apply_terminal_role_idx(idx)

            # Навигация — ТИП (зависит от устройства)
            hit = NAV_TYPE.hits(pos)
            if hit:
                d = 1 if hit == "next" else -1
                if dev == 0:
                    state["chip_type"] = (state["chip_type"] + d) % len(CHIP_TYPES)
                    state["chip_sub"] = 0
                elif dev == 1:
                    state["anom_sub"] = (state["anom_sub"] + d) % len(ANOM_SUBTYPES)

            # Навигация — ПОДТИП (только РАСХОДНИКИ=0 и АДМИНКА=3)
            hit = NAV_SUBTYPE.hits(pos)
            if hit and dev == 0 and state["chip_type"] in (0, 3):
                subs = CHIP_SUBTYPES[state["chip_type"]]
                d = 1 if hit == "next" else -1
                state["chip_sub"] = (state["chip_sub"] + d) % len(subs)

            # Переключатель HP/% для мгновенного лечения
            if dev == 0 and state["chip_type"] == 0 and state["chip_sub"] == 0:
                hit = NAV_HEAL_MODE.hits(pos)
                if hit:
                    state["heal_mode"] = 1 - state.get("heal_mode", 0)
            # Переключатель RAD/% для антирадина
            if dev == 0 and state["chip_type"] == 0 and state["chip_sub"] == 1:
                hit = NAV_HEAL_MODE.hits(pos)
                if hit:
                    state["rad_mode"] = 1 - state.get("rad_mode", 0)
            # Переключатели регенератора (HP + RAD)
            if dev == 0 and state["chip_type"] == 0 and state["chip_sub"] == 2:
                hit = NAV_HEAL_MODE.hits(pos)
                if hit:
                    state["regen_hp_mode"] = 1 - state.get("regen_hp_mode", 0)
                hit = NAV_REGEN_MODE.hits(pos)
                if hit:
                    state["regen_rad_mode"] = 1 - state.get("regen_rad_mode", 0)

            # Переключатель ЕД./% для восстановления
            if dev == 0 and state["chip_type"] == 0 and state["chip_sub"] == 4:
                hit = NAV_HEAL_MODE.hits(pos)
                if hit:
                    state["restore_mode"] = 1 - state.get("restore_mode", 0)
            if dev == 1:
                # Чекбоксы HP-типов (7 шт, 4 в ряд, биты 0-6 непрерывно)
                CB     = 16
                COL_N  = 4
                COL_W  = INNER_W // COL_N
                for i in range(len(DMG_NAMES_CB)):
                    row_i = i // COL_N
                    col_i = i % COL_N
                    y_cb  = row_y(param_start + row_i)
                    x_cb  = PAD + 4 + col_i * COL_W
                    hit_zone = pygame.Rect(x_cb, y_cb, COL_W - 4, ROW_H)
                    if hit_zone.collidepoint(pos):
                        bit = i   # биты 0-6 непрерывно (ПСИ=5, ГРАВИТ.=6)
                        state["anom_dmg_mask"] ^= (1 << bit)   # toggle

                # Строка РАДИАЦИЯ: хит только по чекбоксу+метке (до TextBox)
                # TextBox bx_a_rad_dmg начинается с x = PAD+110
                n_rows = (len(DMG_NAMES_CB) + COL_N - 1) // COL_N  # = 2
                y_rad_row = row_y(param_start + n_rows)
                rad_hit = pygame.Rect(PAD, y_rad_row, 105, ROW_H)  # CB+метка, не доходя до поля
                if rad_hit.collidepoint(pos):
                    state["anom_rad_on"] = not state.get("anom_rad_on", False)

                hit = NAV_RECHARGE.hits(pos)
                if hit:
                    state["anom_recharge"] = 1 - state["anom_recharge"]
                hit = NAV_TARGET.hits(pos)
                if hit:
                    d = 1 if hit == "next" else -1
                    state["anom_target"] = (state["anom_target"] + d) % len(TARGET_MODES)

            # УБЕЖИЩЕ — галочка ВЫБРОС + чекбоксы защит
            if dev == 2:
                sep_y = row_y(param_start + 6) - 2
                emis_hit = pygame.Rect(PAD, sep_y - 10, INNER_W, 20)
                if emis_hit.collidepoint(pos):
                    state["sz_emission"] = not state.get("sz_emission", False)
                # Чекбоксы защит (4 x 2 сетка)
                prot_y_start = param_start + 7
                COL4 = INNER_W // 4
                for i in range(8):
                    ri = i // 4; ci = i % 4
                    y_p = row_y(prot_y_start + ri)
                    x_p = PAD + ci * COL4
                    hit = pygame.Rect(x_p, y_p, COL4, ROW_H)
                    if hit.collidepoint(pos):
                        state["sz_prot_mask"] = state.get("sz_prot_mask", 0) ^ (1 << i)

            # ПДА КОНФИГ — переключатель режима + чекбоксы функций
            if dev == 3:
                hit = NAV_PDA_MODE.hits(pos)
                if hit:
                    d = 1 if hit == "next" else -1
                    state["pda_mode"] = (state.get("pda_mode", 0) + d) % 2
                # Чекбоксы функций (только в режиме ФУНКЦИИ)
                if state.get("pda_mode", 0) == 0:
                    CB = 14; COL2 = INNER_W // 2
                    for i in range(8):
                        ri = i // 2; ci = i % 2
                        y_f = row_y(param_start + 1 + ri)
                        x_f = PAD + ci * COL2
                        hit_r = pygame.Rect(x_f, y_f, COL2, ROW_H)
                        if hit_r.collidepoint(pos):
                            state["pda_func_flags"] = state.get("pda_func_flags", 0xFF) ^ (1 << i)

            # COM-порт
            hit = NAV_COM.hits(pos)
            if hit:
                d = 1 if hit == "next" else -1
                com_ports = get_com_ports()
                state["com_idx"] = (state["com_idx"] + d) % len(com_ports)
                save_state()
                if is_terminal_page():
                    trigger_terminal_scan()

            # ПРОШИТЬ
            if BTN_FLASH.hit(pos):
                collect_state()
                flash_err = ""
                if is_terminal_page():
                    flash_ok = flash_terminal_role()
                    if not flash_ok:
                        flash_err = "Терминал не ответил. Проверь COM и статус [OK]."
                else:
                    cfg_str = build_config_str()
                    if not SERIAL_AVAILABLE:
                        flash_ok = False
                        flash_err = "pyserial не установлен"
                    elif not serial_link.connected:
                        flash_ok = False
                        flash_err = "ESP не подключен. Закрой Serial Monitor и кликни строку статуса."
                    elif serial_link.dev_type != "CHIP_BOX":
                        flash_ok = False
                        flash_err = f"Нужен CHIP_BOX, сейчас {serial_link.dev_type or 'нет устройства'}"
                    elif not cfg_str:
                        flash_ok = False
                        flash_err = "Пустой конфиг чипа"
                    else:
                        flash_ok = serial_link.write_config(cfg_str)
                        if not flash_ok:
                            flash_err = serial_link.last_error or "ESP не записал чип"
                flash_time = now

            # РЕСКАНИРОВАТЬ порт (двойной клик на строку статуса)
            scan_rect = pygame.Rect(0, 44, WIN_W, 24)
            if scan_rect.collidepoint(pos) and SERIAL_AVAILABLE:
                com_ports = get_com_ports()
                target = com_ports[state["com_idx"] % len(com_ports)]
                serial_link.scan(target)

        if event.type == pygame.KEYDOWN:
            for bx in active_boxes:
                if bx.active:
                    bx.key(event)

    # ── ОТРИСОВКА ────────────────────────────────────────────
    screen.fill(C_BG)

    # — Шапка —
    pygame.draw.rect(screen, C_HEADER_BG, (0, 0, WIN_W, 74))
    pygame.draw.line(screen, accent, (0, 74), (WIN_W, 74), 2)

    if flash_ok and now - flash_time < 1.8:
        title = "[OK] ПРОШИТО УСПЕШНО!"
        title_col = (100, 255, 130)
    elif flash_err and now - flash_time < 4.0:
        title = "[ERR] " + flash_err
        title_col = (255, 110, 110)
    else:
        flash_ok = False
        title = "STALKER -- PROGRAMMER v1.0"
        title_col = accent

    tx = F_LG.render(title, True, title_col)
    screen.blit(tx, tx.get_rect(center=(WIN_W // 2, 20)))

    # Статус соединения
    port_name = com_ports[state["com_idx"] % len(com_ports)]
    if SERIAL_AVAILABLE:
        status_txt = serial_link.status_text()
        status_col = serial_link.status_color()
    else:
        status_txt = f"pyserial не установлен  |  Порт: {port_name}"
        status_col = C_DIM
    stx = F_SM.render(status_txt, True, status_col)
    screen.blit(stx, stx.get_rect(center=(WIN_W // 2, 46)))
    # Подсказка: кликни для ресканирования
    if not SERIAL_AVAILABLE or (not serial_link.connected and not serial_link.scanning):
        hint = F_XS.render("↑ кликни для поиска устройства", True, (80, 80, 100))
        screen.blit(hint, hint.get_rect(center=(WIN_W // 2, 62)))

    # Декоративная полоска акцентного цвета слева
    pygame.draw.rect(screen, accent, (0, 0, 4, WIN_H))

    # — Ряд 0: УСТРОЙСТВО —
    dev_label = f"УСТРОЙСТВО: {DEVICES[dev]}"
    NAV_DEVICE.draw(screen, dev_label, accent, bg=C_PANEL)

    # — Ряды 1 и 2: роль терминала или тип чипа —
    if is_terminal_page():
        if txn_ready() and has_terminal_role(serial_link.dev_type):
            sync_terminal_role_from_link()
        ridx = state.get("terminal_role_idx", 0) % len(TERMINAL_ROLES)
        role = TERMINAL_ROLES[ridx]
        NAV_TERMINAL_ROLE.draw(
            screen,
            f"РОЛЬ: {TERMINAL_ROLE_LABELS.get(role, role)} ({role})",
            (200, 220, 255), bg=C_PANEL)
        draw_sep(screen, row_y(2) - 3)
        param_start = 3

    elif dev == 0:
        ct = state["chip_type"]
        NAV_TYPE.draw(screen, f"ТИП: {CHIP_TYPES[ct]}", C_TEXT, bg=C_PANEL)
        if ct in (0, 3):  # РАСХОДНИКИ и АДМИНКА — показать подтип
            subs = CHIP_SUBTYPES[ct]
            # Защита: зажимаем chip_sub если он вышел за пределы (например, после переименования)
            if state["chip_sub"] >= len(subs):
                state["chip_sub"] = 0
            NAV_SUBTYPE.draw(screen,
                             f"ПОДТИП: {subs[state['chip_sub']]}",
                             (200, 200, 255), bg=C_PANEL)
            draw_sep(screen, SEP_Y)
            param_start = 4
        else:  # БРОНЯ и АРТЕФАКТЫ — без подтипа, ряд пропускаем
            draw_sep(screen, row_y(2) - 3)
            param_start = 3
    else:
        draw_sep(screen, row_y(2) - 3)
        param_start = 2

    # ── ПАРАМЕТРЫ по режиму

    if is_terminal_page():
        draw_sep(screen, SEP_Y)

    elif dev == 0:
        # ---------- ЧИП ----------
        rows, has_art_dmg = chip_params_fields()
        ct = state["chip_type"]
        st = state["chip_sub"]

        for i, (lbl, unit, bx) in enumerate(rows):
            y = row_y(param_start + i)
            place(bx, y)
            bx.rect.x = BOX_X   # сброс X: мог быть изменён предыдущим режимом (арт, броня)
            bx.rect.w = BOX_W   # сброс W: аналогично
            draw_row_label(screen, y, lbl, unit)
            bx.draw(screen)

        # ---------- ДОПУСК В ИГРУ: подсказка о 3-мин предупреждении ----------
        if ct == 3 and st == 6:
            y_warn = row_y(param_start + len(rows))
            warn_col = (255, 200, 60)  # жёлтый акцент
            warn_text = F_SM.render("⚠  ПРЕДУПРЕЖДЕНИЕ ЗА 3 МИН ДО ДОПУСКА", True, warn_col)
            screen.blit(warn_text, warn_text.get_rect(centerx=WIN_W//2, y=y_warn + (ROW_H - 12)//2))
        # ---------- ВОСКРЕШЕНИЕ: пояснение ----------
        if ct == 3 and st == 0:
            y_warn = row_y(param_start)
            warn_col = (100, 200, 255)
            warn_text = F_SM.render("Любой мастер · heal при смерти", True, warn_col)
            screen.blit(warn_text, warn_text.get_rect(centerx=WIN_W//2, y=y_warn + (ROW_H - 12)//2))
        # ---------- СБРОС: описание действия ----------
        if ct == 3 and st == 4:
            col_info = (100, 200, 255)  # голубой
            y0 = row_y(param_start)
            screen.blit(F_SM.render("ⓘ  HP → МАКСИМУМ   RAD → 0", True, col_info),
                        (WIN_W//2 - 120, y0 + 4))
            screen.blit(F_SM.render("ⓘ  ВСЕ ЭФФЕКТЫ → 0   ДЕНЬГИ → СТАРТОВЫЕ", True, col_info),
                        (WIN_W//2 - 120, y0 + 4 + 18))
        # ---------- НЕЙТРАЛИЗАЦИЯ: красное предупреждение ----------
        if ct == 3 and st == 5:
            col_kill = (220, 60, 60)  # красный
            y0 = row_y(param_start)
            kill_title = F_MD.render("☠   НЕЙТРАЛИЗАЦИЯ — УБИТЬ ИГРОКА", True, col_kill)
            kill_sub   = F_SM.render("Переводит в режим ожидания воскрешения (is_dead)", True, col_kill)
            screen.blit(kill_title, kill_title.get_rect(centerx=WIN_W//2, y=y0))
            screen.blit(kill_sub,   kill_sub.get_rect(centerx=WIN_W//2,   y=y0 + 24))
        # ---------- БРОНЯ: интервал регена + сетка защит 2×4 ----------
        if ct == 1:
            # Интервал регена ЧЧ:ММ:СС
            y_int = row_y(param_start + len(rows))
            draw_row_label(screen, y_int, "ИНТЕРВАЛ РЕГЕНА:", "")
            bx_arm_int_hh.rect.topleft = (BOX_X,       y_int)
            bx_arm_int_mm.rect.topleft = (BOX_X + 52,  y_int)
            bx_arm_int_ss.rect.topleft = (BOX_X + 104, y_int)
            bx_arm_int_hh.draw(screen)
            bx_arm_int_mm.draw(screen)
            bx_arm_int_ss.draw(screen)
            colon_arm_y = y_int + (ROW_H - 14) // 2
            screen.blit(F_MD.render(":", True, C_DIM), (BOX_X + 46, colon_arm_y))
            screen.blit(F_MD.render(":", True, C_DIM), (BOX_X + 98, colon_arm_y))
            # Разделитель перед сеткой
            draw_sep(screen, row_y(param_start + len(rows) + 1) - 2)
            # Сетка защит 2 колонки × 4 строки (8 типов)
            ARM_COL2 = INNER_W // 2
            ARM_LBL  = 80
            ARM_BOX  = 44
            arm_grid_y0 = param_start + len(rows) + 1
            for i2, (lname, lcol) in enumerate(zip(CHIP_PROT_LABELS, CHIP_PROT_COLORS)):
                ri = i2 // 2; ci = i2 % 2
                y_p = row_y(arm_grid_y0 + ri)
                x_p = PAD + ci * ARM_COL2
                screen.blit(F_XS.render(lname + ":", True, lcol),
                            (x_p, y_p + (ROW_H - 10) // 2))
                bx_p = bx_s[i2]
                bx_p.rect.x      = x_p + ARM_LBL
                bx_p.rect.y      = y_p
                bx_p.rect.width  = ARM_BOX
                bx_p.rect.height = ROW_H
                bx_p.draw(screen)
                screen.blit(F_XS.render("%", True, C_DIM),
                            (x_p + ARM_LBL + ARM_BOX + 2, y_p + (ROW_H - 10) // 2))
            # ДОП. HP — бонус MaxHP при надевании брони
            y_bonus = row_y(arm_grid_y0 + 4)
            draw_sep(screen, y_bonus - 3)
            place(bx_s[9], y_bonus)
            bx_s[9].rect.x = BOX_X
            bx_s[9].rect.w = BOX_W
            draw_row_label(screen, y_bonus, "ДОП. HP:", "HP")
            bx_s[9].draw(screen)
        # СТИМУЛЯТОР: сетка защит 2×4 (без регена, с длительностью)
        if ct == 0 and st == 3:
            # Длительность рендерится через rows (param_start+0)
            # Грид защит рендерится ниже
            stim_grid_y0 = param_start + len(rows)  # после ячейки ДЛИТЕЛЬНОСТЬ
            draw_sep(screen, row_y(stim_grid_y0) - 2)
            ARM_COL2 = INNER_W // 2
            ARM_LBL  = 80
            ARM_BOX  = 44
            for i2, (lname, lcol) in enumerate(zip(CHIP_PROT_LABELS, CHIP_PROT_COLORS)):
                ri = i2 // 2; ci = i2 % 2
                y_p = row_y(stim_grid_y0 + ri)
                x_p = PAD + ci * ARM_COL2
                screen.blit(F_XS.render(lname + ":", True, lcol),
                            (x_p, y_p + (ROW_H - 10) // 2))
                bx_p = bx_s[i2]
                bx_p.rect.x      = x_p + ARM_LBL
                bx_p.rect.y      = y_p
                bx_p.rect.width  = ARM_BOX
                bx_p.rect.height = ROW_H
                bx_p.draw(screen)
                screen.blit(F_XS.render("%", True, C_DIM),
                            (x_p + ARM_LBL + ARM_BOX + 2, y_p + (ROW_H - 10) // 2))
        # МГНОВЕННОЕ ЛЕЧЕНИЕ: NavRow переключатель HP/%
        if ct == 0 and st == 0:
            hm = state.get("heal_mode", 0)
            mode_names = ["В ЕДИНИЦАХ HP", "В ПРОЦЕНТАХ %"]
            y_hm = row_y(param_start + len(rows))
            NAV_HEAL_MODE.rect.topleft = (PAD, y_hm)
            NAV_HEAL_MODE.btn_prev.rect.topleft = (PAD, y_hm)
            NAV_HEAL_MODE.btn_next.rect.topleft = (PAD + INNER_W - 36, y_hm)
            NAV_HEAL_MODE.inner = pygame.Rect(PAD + 40, y_hm, INNER_W - 80, ROW_H)
            col_hm = (255, 200, 80) if hm == 1 else C_TEXT
            NAV_HEAL_MODE.draw(screen, f"РЕЖИМ: {mode_names[hm]}", col_hm, bg=C_PANEL)
        # АНТИРАДИН: NavRow переключатель RAD/%
        if ct == 0 and st == 1:
            rm = state.get("rad_mode", 0)
            mode_names_r = ["В ЕДИНИЦАХ RAD", "% ОТ ТЕКУЩЕГО RAD"]
            y_rm = row_y(param_start + len(rows))
            NAV_HEAL_MODE.rect.topleft = (PAD, y_rm)
            NAV_HEAL_MODE.btn_prev.rect.topleft = (PAD, y_rm)
            NAV_HEAL_MODE.btn_next.rect.topleft = (PAD + INNER_W - 36, y_rm)
            NAV_HEAL_MODE.inner = pygame.Rect(PAD + 40, y_rm, INNER_W - 80, ROW_H)
            col_rm = (100, 200, 255) if rm == 1 else C_TEXT  # голубой для RAD-режима
            NAV_HEAL_MODE.draw(screen, f"РЕЖИМ: {mode_names_r[rm]}", col_rm, bg=C_PANEL)
        # ВОССТАНОВЛЕНИЕ: NavRow ед./% (cap = макс. прочность)
        if ct == 0 and st == 4:
            rm2 = state.get("restore_mode", 0)
            mode_rst = ["В ЕДИНИЦАХ", "% ОТ МАКС.ПРОЧНОСТИ"]
            y_rt = row_y(param_start + len(rows))
            NAV_HEAL_MODE.rect.topleft  = (PAD, y_rt)
            NAV_HEAL_MODE.btn_prev.rect.topleft = (PAD, y_rt)
            NAV_HEAL_MODE.btn_next.rect.topleft = (PAD + INNER_W - 36, y_rt)
            NAV_HEAL_MODE.inner = pygame.Rect(PAD + 40, y_rt, INNER_W - 80, ROW_H)
            col_rt = (120, 220, 255) if rm2 == 1 else C_TEXT
            NAV_HEAL_MODE.draw(screen, f"РЕЖИМ: {mode_rst[rm2]}", col_rt, bg=C_PANEL)
        # РЕГЕНЕРАТОР: два NavRow (режим HP + режим RAD) под полями
        if ct == 0 and st == 2:
            rhm = state.get("regen_hp_mode", 0)
            rrm = state.get("regen_rad_mode", 0)
            mn_hp  = ["В ЕДИН. HP",      "% MaxHP"]
            mn_rad = ["В ЕДИН. RAD",     "% тек.RAD"]
            draw_sep(screen, row_y(param_start + len(rows)) - 2)
            # NavRow HP-режим
            y_rh = row_y(param_start + len(rows))
            NAV_HEAL_MODE.rect.topleft  = (PAD, y_rh)
            NAV_HEAL_MODE.btn_prev.rect.topleft = (PAD, y_rh)
            NAV_HEAL_MODE.btn_next.rect.topleft = (PAD + INNER_W - 36, y_rh)
            NAV_HEAL_MODE.inner = pygame.Rect(PAD + 40, y_rh, INNER_W - 80, ROW_H)
            c_rh = (255, 200, 80) if rhm == 1 else C_TEXT
            NAV_HEAL_MODE.draw(screen, f"HP: {mn_hp[rhm]}", c_rh, bg=C_PANEL)
            # NavRow RAD-режим
            y_rr = row_y(param_start + len(rows) + 1)
            NAV_REGEN_MODE.rect.topleft  = (PAD, y_rr)
            NAV_REGEN_MODE.btn_prev.rect.topleft = (PAD, y_rr)
            NAV_REGEN_MODE.btn_next.rect.topleft = (PAD + INNER_W - 36, y_rr)
            NAV_REGEN_MODE.inner = pygame.Rect(PAD + 40, y_rr, INNER_W - 80, ROW_H)
            c_rr = (100, 200, 255) if rrm == 1 else C_TEXT
            NAV_REGEN_MODE.draw(screen, f"RAD: {mn_rad[rrm]}", c_rr, bg=C_PANEL)
        # Для АРТЕФАКТОВ: рендер ИНТЕРВАЛА регена ЧЧ:ММ:СС
        art_int_row_offset = 0
        if ct == 2:
            y_int = row_y(param_start + len(rows))
            draw_row_label(screen, y_int, "ИНТЕРВАЛ РЕГЕНА:", "")
            bx_art_int_hh.rect.topleft = (BOX_X, y_int)
            bx_art_int_mm.rect.topleft = (BOX_X + 52, y_int)
            bx_art_int_ss.rect.topleft = (BOX_X + 104, y_int)
            bx_art_int_hh.draw(screen)
            bx_art_int_mm.draw(screen)
            bx_art_int_ss.draw(screen)
            colon_int_y = y_int + (ROW_H - 12) // 2
            screen.blit(F_MD.render(":", True, C_DIM), (BOX_X + 46, colon_int_y))
            screen.blit(F_MD.render(":", True, C_DIM), (BOX_X + 98, colon_int_y))
            art_int_row_offset = 1  # смещение art-таблицы

        # Для АРТЕФАКТОВ: компактная таблица (защита % | урон HP | интервал ЧЧ:ММ:СС)
        if has_art_dmg and ct == 2:
            # Константы позиционирования колонок
            ART_LBLW  = 58
            ART_RES_W = 50   # широко — 4 цифры
            ART_DMG_W = 50   # широко — 4 цифры
            ART_TW    = 22   # ширина поля ЧЧ/ММ/СС
            ART_RES_X = PARAMS_X + ART_LBLW + 2
            ART_DMG_X = ART_RES_X + ART_RES_W + 4
            ART_HH_X  = ART_DMG_X + ART_DMG_W + 8
            ART_MM_X  = ART_HH_X  + ART_TW + 5
            ART_SS_X  = ART_MM_X  + ART_TW + 5

            # rows = [рег], 1 строка + 1 ряд ИНТЕРВАЛ уже отрисованы
            art_y_hdr = row_y(param_start + len(rows) + art_int_row_offset)

            # Двухстрочные заголовки колонок
            hdr_y1 = art_y_hdr + 3
            hdr_y2 = art_y_hdr + 14
            # Колонка 1: ЗАЩИТА / %
            screen.blit(F_XS.render("ЗАЩИТА", True, (100, 220, 100)), (ART_RES_X, hdr_y1))
            screen.blit(F_XS.render("%",      True, (100, 220, 100)), (ART_RES_X + 18, hdr_y2))
            # Колонка 2: УРОН / HP
            screen.blit(F_XS.render("УРОН",   True, (220, 100, 100)), (ART_DMG_X + 8, hdr_y1))
            screen.blit(F_XS.render("HP",     True, (220, 100, 100)), (ART_DMG_X + 14, hdr_y2))
            # Колонка 3: таймер
            screen.blit(F_XS.render("ИНТЕРВАЛ", True, C_DIM), (ART_HH_X, hdr_y1))

            # Типы 0-6: HP-урон
            for j in range(7):
                lbl = CHIP_PROT_LABELS[j]
                col = CHIP_PROT_COLORS[j]
                y = row_y(param_start + len(rows) + art_int_row_offset + 1 + j)
                screen.blit(F_SM.render(lbl + ":", True, col),
                            (PARAMS_X, y + (ROW_H - 12) // 2))
                bx_res = bx_s[j + 1]
                bx_res.rect.x = ART_RES_X; bx_res.rect.y = y; bx_res.rect.w = ART_RES_W
                bx_res.draw(screen)
                bx_dmg = bx_s[j + 9]
                bx_dmg.rect.x = ART_DMG_X; bx_dmg.rect.y = y; bx_dmg.rect.w = ART_DMG_W
                bx_dmg.draw(screen)
                bx_art_hh[j].rect.topleft = (ART_HH_X, y)
                bx_art_mm[j].rect.topleft = (ART_MM_X, y)
                bx_art_ss[j].rect.topleft = (ART_SS_X, y)
                bx_art_hh[j].draw(screen)
                bx_art_mm[j].draw(screen)
                bx_art_ss[j].draw(screen)
                colon_y = y + (ROW_H - 12) // 2
                screen.blit(F_SM.render(":", True, C_DIM), (ART_HH_X + ART_TW + 1, colon_y))
                screen.blit(F_SM.render(":", True, C_DIM), (ART_MM_X + ART_TW + 1, colon_y))

            # Тип 7 (RAD) — два ряда: заголовок + поля ввода
            j = 7
            y_rad_hdr = row_y(param_start + len(rows) + art_int_row_offset + 1 + j)
            y_rad_inp = row_y(param_start + len(rows) + art_int_row_offset + 1 + j + 1)
            col_rad = CHIP_PROT_COLORS[7]
            draw_sep(screen, y_rad_hdr - 2)   # разделитель перед RAD-секцией
            # Заголовки колонок (отдельный ряд)
            screen.blit(F_XS.render("ЗАЩИТА", True, col_rad), (ART_RES_X, y_rad_hdr + 3))
            screen.blit(F_XS.render("%",      True, col_rad), (ART_RES_X + 18, y_rad_hdr + 14))
            screen.blit(F_XS.render("RAD",    True, col_rad), (ART_DMG_X + 8,  y_rad_hdr + (ROW_H - 10) // 2))
            screen.blit(F_XS.render("ИНТЕРВАЛ", True, C_DIM), (ART_HH_X,       y_rad_hdr + 3))
            # Метка + поля ввода (следующий ряд)
            screen.blit(F_SM.render("РАДИАЦИЯ:", True, col_rad),
                        (PARAMS_X, y_rad_inp + (ROW_H - 12) // 2))
            bx_res = bx_s[8]
            bx_res.rect.x = ART_RES_X; bx_res.rect.y = y_rad_inp; bx_res.rect.w = ART_RES_W
            bx_res.draw(screen)
            bx_rad_dmg = bx_s[16]
            bx_rad_dmg.rect.x = ART_DMG_X; bx_rad_dmg.rect.y = y_rad_inp; bx_rad_dmg.rect.w = ART_DMG_W
            bx_rad_dmg.draw(screen)
            bx_art_hh[7].rect.topleft = (ART_HH_X, y_rad_inp)
            bx_art_mm[7].rect.topleft = (ART_MM_X, y_rad_inp)
            bx_art_ss[7].rect.topleft = (ART_SS_X, y_rad_inp)
            bx_art_hh[7].draw(screen)
            bx_art_mm[7].draw(screen)
            bx_art_ss[7].draw(screen)
            colon_y = y_rad_inp + (ROW_H - 12) // 2
            screen.blit(F_SM.render(":", True, C_DIM), (ART_HH_X + ART_TW + 1, colon_y))
            screen.blit(F_SM.render(":", True, C_DIM), (ART_MM_X + ART_TW + 1, colon_y))


        # ПРОЧНОСТЬ/КОЛИЧЕСТВО — фиксированно внизу, прямо над COM-портом
        uses_labels = {
            0: ("КОЛИЧЕСТВО (ШТ):",  "ШТ"),
            1: ("ПРОЧНОСТЬ:",        "зарядов"),
            2: ("ПРОЧНОСТЬ:",        "зарядов"),
            3: ("ИСПОЛЬЗОВАНИЙ:",    "РАЗ"),
        }
        uses_lbl, uses_unit = uses_labels.get(ct, ("ПРОЧНОСТЬ:", "шт"))
        uses_y = WIN_H - 88 - ROW_H - 10    # прямо над NAV_COM
        draw_sep(screen, uses_y)
        place(bx_uses, uses_y + 4)
        draw_row_label(screen, uses_y + 4, uses_lbl, uses_unit)
        bx_uses.draw(screen)

    elif dev == 1:
        # ---------- АНОМАЛИЯ ----------
        p = param_start

        # ── Чекбоксы типов урона (2 колонки × 4 ряда) ──────
        CB   = 16                       # размер квадратика
        CB    = 16
        COL_N = 4
        COL_W = INNER_W // COL_N
        mask = state.get("anom_dmg_mask", 1)
        for i, (name, color) in enumerate(zip(DMG_NAMES_CB, DMG_COLORS_CB)):
            row_i = i // COL_N
            col_i = i % COL_N
            y_cb  = row_y(p + row_i)
            x_cb  = PAD + 4 + col_i * COL_W
            checked = bool(mask & (1 << i))  # биты 0-6, непрерывно
            cb_rect = pygame.Rect(x_cb, y_cb + (ROW_H - CB) // 2, CB, CB)
            pygame.draw.rect(screen, color if checked else C_BOX, cb_rect, border_radius=3)
            pygame.draw.rect(screen, color if checked else C_LINE, cb_rect, 2, border_radius=3)
            if checked:
                pygame.draw.rect(screen, color, cb_rect.inflate(-4, -4), border_radius=2)
                cx, cy = cb_rect.centerx, cb_rect.centery
                pygame.draw.line(screen, C_WHITE, (cx-4, cy), (cx-1, cy+3), 2)
                pygame.draw.line(screen, C_WHITE, (cx-1, cy+3), (cx+4, cy-3), 2)
            tx_cb = F_XS.render(name, True, color if checked else C_DIM)
            screen.blit(tx_cb, (x_cb + CB + 3, y_cb + (ROW_H - 11) // 2))
        n_rows = (len(DMG_NAMES_CB) + COL_N - 1) // COL_N  # = 2
        p += n_rows

        # ── Строка РАДИАЦИЯ (две колонки) ────────────────────
        rad_on = state.get("anom_rad_on", False)
        y_rr   = row_y(p)
        c_rad  = RAD_COLOR if rad_on else C_DIM
        # Чекбокс РАДИАЦИЯ
        cb_r = pygame.Rect(PAD + 4, y_rr + (ROW_H - CB) // 2, CB, CB)
        pygame.draw.rect(screen, RAD_COLOR if rad_on else C_BOX, cb_r, border_radius=3)
        pygame.draw.rect(screen, RAD_COLOR if rad_on else C_LINE, cb_r, 2, border_radius=3)
        if rad_on:
            pygame.draw.rect(screen, RAD_COLOR, cb_r.inflate(-4, -4), border_radius=2)
            cx2, cy2 = cb_r.centerx, cb_r.centery
            pygame.draw.line(screen, C_WHITE, (cx2-4, cy2), (cx2-1, cy2+3), 2)
            pygame.draw.line(screen, C_WHITE, (cx2-1, cy2+3), (cx2+4, cy2-3), 2)
        screen.blit(F_XS.render("РАДИАЦИЯ", True, c_rad), (PAD + 24, y_rr + (ROW_H-11)//2))
        # Левая пол.: кол-во RAD/уд
        if rad_on:
            bx_a_rad_dmg.rect.topleft = (PAD + 110, y_rr)
            bx_a_rad_dmg.draw(screen)
            screen.blit(F_XS.render("RAD/уд", True, C_DIM), (PAD + 166, y_rr + (ROW_H-11)//2))
        else:
            screen.blit(F_SM.render("—", True, C_DIM), (PAD + 122, y_rr + (ROW_H-14)//2))
        # Правая пол.: таймер ЧЧ:ММ:СС
        TX = PAD + 215  # сдвинуто правее чтобы не перекрывать "RAD/уд"
        if rad_on:
            bx_a_rh.rect.topleft  = (TX,       y_rr)
            bx_a_rm.rect.topleft  = (TX + 50,  y_rr)
            bx_a_rs.rect.topleft  = (TX + 100, y_rr)
            bx_a_rh.draw(screen); bx_a_rm.draw(screen); bx_a_rs.draw(screen)
            coly2 = y_rr + (ROW_H - 14) // 2
            screen.blit(F_MD.render(":", True, C_DIM), (TX + 44, coly2))
            screen.blit(F_MD.render(":", True, C_DIM), (TX + 94, coly2))
        else:
            screen.blit(F_SM.render("ЧЧ:ММ:СС", True, C_DIM), (TX, y_rr + (ROW_H-11)//2))
        p += 1  # RAD-строка

        # Подсказка если ничего не выбрано
        if mask == 0:
            warn = F_XS.render("⚠  выбери хотя бы один тип!", True, (220, 100, 60))
            screen.blit(warn, warn.get_rect(center=(WIN_W//2, row_y(param_start+3) + ROW_H//2)))

        draw_sep(screen, row_y(p + 1) - 3)

        labels_a = [
            ("СТАРТ УРОН:", "HP", bx_a_dmg),
            ("МАКС УРОН:",  "HP", bx_a_dmax),
            ("РОСТ/МИН:",   "ЕД.", bx_a_dstep),
        ]

        for i, (lbl, unit, bx) in enumerate(labels_a):
            y = row_y(p + 1 + i)
            place(bx, y)
            draw_row_label(screen, y, lbl, unit)
            bx.draw(screen)

        # Перезарядка (ЧЧ:ММ:СС)
        y = row_y(p + 4)

        draw_row_label(screen, y, "ПЕРЕЗАРЯДКА:", "")
        bx_a_h.rect.topleft  = (BOX_X,       y)
        bx_a_m.rect.topleft  = (BOX_X + 52,  y)
        bx_a_s2.rect.topleft = (BOX_X + 104, y)
        bx_a_h.draw(screen); bx_a_m.draw(screen); bx_a_s2.draw(screen)
        colon_y = y + (ROW_H - 14) // 2
        screen.blit(F_MD.render(":", True, C_DIM), (BOX_X + 46, colon_y))
        screen.blit(F_MD.render(":", True, C_DIM), (BOX_X + 98, colon_y))

        # Режим перезарядки
        NAV_RECHARGE.rect.topleft = (PAD, row_y(p + 5))
        NAV_RECHARGE.btn_prev.rect.topleft = (PAD, row_y(p + 5))
        NAV_RECHARGE.btn_next.rect.topleft = (PAD + INNER_W - 36, row_y(p + 5))
        NAV_RECHARGE.inner = pygame.Rect(PAD + 40, row_y(p + 5), INNER_W - 80, ROW_H)
        NAV_RECHARGE.draw(screen,
                          f"РЕЖИМ: {RECHARGE_MODES[state['anom_recharge']]}",
                          (255, 200, 80), bg=C_PANEL)

        # Режим цели
        NAV_TARGET.rect.topleft = (PAD, row_y(p + 6))
        NAV_TARGET.btn_prev.rect.topleft = (PAD, row_y(p + 6))
        NAV_TARGET.btn_next.rect.topleft = (PAD + INNER_W - 36, row_y(p + 6))
        NAV_TARGET.inner = pygame.Rect(PAD + 40, row_y(p + 6), INNER_W - 80, ROW_H)
        NAV_TARGET.draw(screen,
                        f"ЦЕЛИ: {TARGET_MODES[state['anom_target']]}",
                        (255, 160, 60), bg=C_PANEL)

        # Параметры облака (только в режиме CLOUD)
        if state["anom_target"] == 2:
            y = row_y(p + 7)
            place(bx_a_erupt, y)
            draw_row_label(screen, y, "ДЛИТ. ОБЛАКА:", "СЕК")
            bx_a_erupt.draw(screen)

            y = row_y(p + 8)
            place(bx_a_hits, y)
            draw_row_label(screen, y, "К-ВО УДАРОВ:", "шт")
            bx_a_hits.draw(screen)

            y = row_y(p + 9)
            place(bx_a_rad, y)
            draw_row_label(screen, y, "РАДИУС:", "МЕТРОВ")
            bx_a_rad.draw(screen)
        else:
            screen.blit(F_MD.render("ДЛИТ. ОБЛАКА:  —", True, C_DIM),
                        (PAD, row_y(p + 7) + (ROW_H-14)//2))
            screen.blit(F_MD.render("К-ВО УДАРОВ:  —", True, C_DIM),
                        (PAD, row_y(p + 8) + (ROW_H-14)//2))

            y = row_y(p + 9)
            place(bx_a_rad, y)
            draw_row_label(screen, y, "РАДИУС:", "МЕТРОВ")
            bx_a_rad.draw(screen)


    elif dev == 2:
        # ---------- УБЕЖИЩЕ ----------
        ACC = (160, 255, 180)  # зелёный акцент

        # --- Реген HP ---
        y = row_y(param_start)
        place(bx_sz_regen, y)
        draw_row_label(screen, y, "РЕГЕН HP/МИН:", "HP/мин", accent=ACC)
        bx_sz_regen.draw(screen)

        # --- Таймер HP ЧЧ:ММ:СС ---
        y = row_y(param_start + 1)
        draw_row_label(screen, y, "ИНТЕРВАЛ РЕГЕНА:", "", accent=ACC)
        bx_sz_hh.rect.topleft = (BOX_X,       y)
        bx_sz_hm.rect.topleft = (BOX_X + 52,  y)
        bx_sz_hs.rect.topleft = (BOX_X + 104, y)
        bx_sz_hh.draw(screen); bx_sz_hm.draw(screen); bx_sz_hs.draw(screen)
        colon_y = y + (ROW_H - 14) // 2
        screen.blit(F_MD.render(":", True, C_DIM), (BOX_X + 46, colon_y))
        screen.blit(F_MD.render(":", True, C_DIM), (BOX_X + 98, colon_y))

        # --- Очистка RAD ---
        y = row_y(param_start + 2)
        place(bx_sz_rad, y)
        draw_row_label(screen, y, "ОЧИСТКА RAD:", "RAD/мин", accent=ACC)
        bx_sz_rad.draw(screen)

        # --- Таймер RAD ЧЧ:ММ:СС ---
        y = row_y(param_start + 3)
        draw_row_label(screen, y, "ИНТЕРВАЛ ОЧИСТКИ:", "", accent=ACC)
        bx_sz_rh.rect.topleft = (BOX_X,       y)
        bx_sz_rm.rect.topleft = (BOX_X + 52,  y)
        bx_sz_rs.rect.topleft = (BOX_X + 104, y)
        bx_sz_rh.draw(screen); bx_sz_rm.draw(screen); bx_sz_rs.draw(screen)
        colon_y = y + (ROW_H - 14) // 2
        screen.blit(F_MD.render(":", True, C_DIM), (BOX_X + 46, colon_y))
        screen.blit(F_MD.render(":", True, C_DIM), (BOX_X + 98, colon_y))

        # --- Радиус ---
        y = row_y(param_start + 4)
        place(bx_sz_rds, y)
        draw_row_label(screen, y, "РАДИУС ДЕЙСТВИЯ:", "МЕТРОВ", accent=ACC)
        bx_sz_rds.draw(screen)

        # Подсказка
        y_hint = row_y(param_start + 5)
        screen.blit(F_XS.render("0 = эффект не действует", True, C_DIM), (PAD, y_hint))

        # --- Галочка ВЫБРОС (линия выше, текст+чекбокс внутри строки) ---
        sep_y  = row_y(param_start + 6) - (ROW_H + ROW_G) // 2  # линия между строками
        y_em   = row_y(param_start + 6)                          # строка чекбокса
        pygame.draw.line(screen, C_LINE, (PAD, sep_y), (WIN_W - PAD, sep_y))
        emis_on    = state.get("sz_emission", False)
        emis_color = (255, 200, 60)
        CB_SZ = 14
        cb_r = pygame.Rect(PAD + 8, y_em + (ROW_H - CB_SZ) // 2, CB_SZ, CB_SZ)
        pygame.draw.rect(screen, emis_color if emis_on else C_BOX, cb_r, border_radius=3)
        pygame.draw.rect(screen, emis_color, cb_r, 2, border_radius=3)  # рамка всегда жёлтая
        if emis_on:
            pygame.draw.rect(screen, emis_color, cb_r.inflate(-4, -4), border_radius=2)
        lbl = F_MD.render("ЗАЩИТА ОТ ВЫБРОСА", True, ACC)  # всегда зёленый, контрастный
        screen.blit(lbl, (PAD + CB_SZ + 14, y_em + (ROW_H - 14) // 2))

        # --- Сетка % защит (8 типов, 2 колонки) ---
        SZ_PROT_LABELS = ["ВЗРЫВ","КРОВЬ","ТЕРМО","ЭЛЕКТРО",
                          "ХИМИЯ","ПСИ","ГРАВИТ.","RAD"]
        SZ_PROT_COLORS8 = [
            (255, 150,  50), (220,  60,  60), (255,  90,  40), (100, 180, 255),
            (140, 220,  80), (200,  80, 220), (120, 120, 220), (100, 220, 120),
        ]
        COL2 = INNER_W // 2
        LBL2 = 74
        BOX2 = 42
        prot_y_start = param_start + 7
        for i, (lname, lcol) in enumerate(zip(SZ_PROT_LABELS, SZ_PROT_COLORS8)):
            ri = i // 2; ci = i % 2
            y_p = row_y(prot_y_start + ri)
            x_p = PAD + ci * COL2
            screen.blit(F_XS.render(lname + ":", True, lcol), (x_p, y_p + (ROW_H - 10) // 2))
            bx = bx_sz_prot[i]
            bx.rect.topleft = (x_p + LBL2, y_p)
            bx.rect.width   = BOX2
            bx.rect.height  = ROW_H
            bx.draw(screen)
            screen.blit(F_XS.render("%", True, C_DIM),
                        (x_p + LBL2 + BOX2 + 2, y_p + (ROW_H - 10) // 2))



    elif dev == 3:
        # ---------- ПДА КОНФИГ ----------
        ACC = (120, 200, 255)  # голубой акцент
        mode = state.get("pda_mode", 0)
        modes = ["ФУНКЦИИ", "ПРЕСЕТ ДАННЫХ"]

        # --- Переключатель режима ---
        y_mode = row_y(param_start)
        NAV_PDA_MODE.rect.topleft      = (PAD, y_mode)
        NAV_PDA_MODE.btn_prev.rect.topleft = (PAD, y_mode)
        NAV_PDA_MODE.btn_next.rect.topleft = (PAD + INNER_W - 36, y_mode)
        NAV_PDA_MODE.inner = pygame.Rect(PAD + 40, y_mode, INNER_W - 80, ROW_H)
        NAV_PDA_MODE.draw(screen, f"РЕЖИМ: {modes[mode]}", ACC, bg=C_PANEL)

        draw_sep(screen, row_y(param_start + 1) - 2)

        if mode == 0:  # ---- ФУНКЦИИ ----
            flags = state.get("pda_func_flags", 0xFF)
            FUNC_LABELS = [
                ("HP СИСТЕМА",        (255, 100, 100)),
                ("РАДИАЦИЯ",          (100, 220, 120)),
                ("ДЕНЬГИ/ЭКОНОМИКА",   (255, 200,  60)),
                ("БРОНЯ",              (100, 180, 255)),
                ("АРТЕФАКТЫ",         (200, 120, 255)),
                ("АНОМАЛИИ",          (255, 140,  60)),
                ("УРОВНИ/XP",         (140, 220,  80)),
                ("ЧИПЫ-РАСХОДНИКИ",  (180, 180, 180)),
            ]
            CB = 14  # размер чекбокса
            COL2 = INNER_W // 2
            for i, (lbl, col) in enumerate(FUNC_LABELS):
                ri = i // 2; ci = i % 2
                y_f = row_y(param_start + 1 + ri)
                x_f = PAD + ci * COL2
                on  = bool(flags & (1 << i))
                cr = pygame.Rect(x_f + 4, y_f + (ROW_H - CB) // 2, CB, CB)
                pygame.draw.rect(screen, col if on else C_BOX, cr, border_radius=3)
                pygame.draw.rect(screen, col, cr, 2, border_radius=3)
                if on:
                    pygame.draw.rect(screen, col, cr.inflate(-4, -4), border_radius=2)
                screen.blit(F_XS.render(lbl, True, col if on else C_DIM),
                            (x_f + CB + 10, y_f + (ROW_H - 10) // 2))

        else:  # ---- ПРЕСЕТ ДАННЫХ ----
            rows_pda = [
                ("МАКС. HP:",     "HP",  bx_pda_maxhp),
                ("СТАРТ HP:",    "HP",  bx_pda_starthp),
                ("МАКС. RAD:",    "RAD", bx_pda_maxrad),
                ("ДЕНЬГИ (СТАРТ):", "РУБ", bx_pda_money),
                ("УРОВЕНЬ:",     "LVL", bx_pda_level),
                ("ОПЫТ:",         "XP",  bx_pda_xp),
            ]
            for ri, (lbl, unit, bx) in enumerate(rows_pda):
                y_r = row_y(param_start + 1 + ri)
                place(bx, y_r)
                draw_row_label(screen, y_r, lbl, unit, accent=ACC)
                bx.draw(screen)

            draw_sep(screen, row_y(param_start + 7) - 2, label="БАЗОВЫЕ ЗАЩИТЫ")

            # Сетка 8 защит 2×4
            COL2 = INNER_W // 2
            LBL2 = 74; BOX2 = 42
            for i, (lname, lcol) in enumerate(zip(CHIP_PROT_LABELS, CHIP_PROT_COLORS)):
                ri = i // 2; ci = i % 2
                y_p = row_y(param_start + 7 + ri)
                x_p = PAD + ci * COL2
                screen.blit(F_XS.render(lname + ":", True, lcol), (x_p, y_p + (ROW_H - 10) // 2))
                bx = bx_pda_prot[i]
                bx.rect.topleft = (x_p + LBL2, y_p)
                bx.rect.width   = BOX2
                bx.rect.height  = ROW_H
                bx.draw(screen)
                screen.blit(F_XS.render("%", True, C_DIM),
                            (x_p + LBL2 + BOX2 + 2, y_p + (ROW_H - 10) // 2))

    # — Нижняя панель: COM-порт + кнопка ПРОШИТЬ —
    pygame.draw.line(screen, C_LINE, (0, WIN_H - 96), (WIN_W, WIN_H - 96))
    NAV_COM.rect.topleft = (PAD, WIN_H - 88)
    NAV_COM.btn_prev.rect.topleft = (PAD, WIN_H - 88)
    NAV_COM.btn_next.rect.topleft = (PAD + INNER_W - 36, WIN_H - 88)
    NAV_COM.inner = pygame.Rect(PAD + 40, WIN_H - 88, INNER_W - 80, ROW_H)
    nav_com_label = f"ПОРТ: {port_name}"
    if SERIAL_AVAILABLE and serial_link.connected:
        nav_com_label = f"ПОРТ: {serial_link.port} [OK]"
    NAV_COM.draw(screen, nav_com_label, C_TEXT, bg=C_PANEL)

    BTN_FLASH.rect.topleft = (PAD + INNER_W // 4, WIN_H - 50)
    BTN_FLASH.rect.width   = INNER_W // 2
    BTN_FLASH.rect.height  = 38
    flash_override = None
    if now - flash_time < 0.4:
        if flash_ok:
            flash_override = C_BTN_OK_FLS
        elif flash_err:
            flash_override = C_BTN_ERR_FLS
    BTN_FLASH.draw(screen, override=flash_override)

    pygame.display.flip()
    clock.tick(60)

pygame.quit()
