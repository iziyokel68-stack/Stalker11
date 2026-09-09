# =====================================================
# ПОЛНОЦЕННЫЙ ЭМУЛЯТОР — Все компоненты в одном окне
# =====================================================
import pygame, time, socket, threading, queue, json, os, math
from game_logic import Player, ItemChip, PROJECT_THEMES, ActiveTask, ACHIEVEMENTS
from protocol import Packet, Msg, CmdSub, DmgType, Emitter, create_damage_packet, create_safezone_packet, SZ_PROT_FLAG, create_zone_prot_packet

pygame.init()
try:
    pygame.mixer.init(frequency=22050, size=-16, channels=1, buffer=512)
except pygame.error:
    pass

WIN_W, WIN_H = 1100, 780
FPS = 30
TFT_W, TFT_H = 160, 128
SCREEN_W, SCREEN_H = 240, 192  # TFT масштаб ~1.5x

# Цвета
C_BG = (18, 18, 22)
C_BODY = (45, 50, 45); C_BODY_EDGE = (35, 38, 35); C_BODY_LIGHT = (60, 65, 60)
C_SCREEN_BEZEL = (20, 20, 20)
C_BTN = (70, 75, 70); C_BTN_HOVER = (90, 95, 90); C_BTN_PRESS = (50, 55, 50); C_BTN_TEXT = (200, 200, 200)
C_TOGGLE_ON = (60, 180, 60); C_TOGGLE_OFF = (120, 50, 50); C_TOGGLE_BG = (30, 30, 30)
C_SLOT_EMPTY = (25, 28, 25); C_SLOT_BORDER = (80, 85, 80); C_SLOT_FULL = (50, 120, 60)
C_SCREW = (90, 90, 80); C_LABEL = (110, 115, 110)
C_PANEL_BG = (28, 28, 35); C_PANEL_BTN = (55, 55, 70); C_PANEL_BTN_H = (75, 75, 95)
T_BG = (0,0,0); T_BORDER = (56,60,56); T_TEXT = (255,255,255)
T_HP_HI = (0,255,0); T_HP_MID = (255,255,0); T_HP_LO = (255,0,0)
T_RAD = (255,165,0); T_MONEY = (0,255,255); T_XP = (180,130,255)
T_SLOT_E = (40,45,40); T_SLOT_F = (100,255,100); T_DEATH = (255,80,80)

# Глобальные списки
world_chips = []
world_anomalies = []
selected_chip_idx = -1
active_textbox = None
event_log = []  # Глобальный лог событий [{"time":..., "src":..., "text":..., "color":...}]
MAX_LOG = 30
NUM_PAGES = 3  # 0 главная, 1 инвентарь, 2 меню (подразделы внутри)
MENU_ITEMS = ("СОПРОТИВЛЕНИЯ", "АКТИВНЫЕ КВЕСТЫ", "ДОСТИЖЕНИЯ", "СДАТЬСЯ")
VIBRO_MS = {
    "damage": 250, "death": 500, "radiation": 200, "level_up": 350,
    "achievement": 350, "rank_ready": 500, "revive": 350, "info": 100,
}
VIBRO_EVENTS = frozenset(VIBRO_MS)

def _play_tone(freq: int, ms: int = 120, volume: float = 0.35):
    """Короткий звуковой сигнал (симуляция DFPlayer)."""
    if not pygame.mixer.get_init():
        return
    try:
        rate = 22050
        n = int(rate * ms / 1000)
        buf = bytearray(n * 2)
        for i in range(n):
            val = int(32767 * volume * math.sin(2 * math.pi * freq * i / rate))
            buf[i * 2] = val & 0xFF
            buf[i * 2 + 1] = (val >> 8) & 0xFF
        snd = pygame.mixer.Sound(buffer=bytes(buf))
        snd.play()
    except Exception:
        pass

def _notify_color(ntype: str):
    return {
        "achievement": T_XP,
        "rank_ready": (255, 220, 80),
        "rank_confirmed": T_MONEY,
        "level_up": T_HP_HI,
    }.get(ntype, T_HP_MID)

# =====================================================
# UI КЛАССЫ (ПДА)
# =====================================================
class Button:
    def __init__(s, x, y, w, h, label, action):
        s.rect = pygame.Rect(x, y, w, h); s.label = label; s.action = action
        s.hovered = False; s.pressed = False; s.press_time = 0
    def draw(s, surface, font):
        c = C_BTN_PRESS if s.pressed else (C_BTN_HOVER if s.hovered else C_BTN)
        pygame.draw.rect(surface, c, s.rect, border_radius=6)
        pygame.draw.rect(surface, C_BODY_EDGE, s.rect, 2, border_radius=6)
        t = font.render(s.label, True, C_BTN_TEXT)
        surface.blit(t, t.get_rect(center=s.rect.center))
    def check_hover(s, mx, my):
        s.hovered = s.rect.collidepoint(mx, my)
        if s.pressed and time.time() - s.press_time > 0.15: s.pressed = False
        return s.hovered
    def click(s):
        s.pressed = True; s.press_time = time.time(); return s.action

class Toggle:
    def __init__(s, x, y, w, h, label, state=True):
        s.rect = pygame.Rect(x, y, w, h); s.label = label; s.state = state
    def draw(s, surface, font):
        pygame.draw.rect(surface, C_TOGGLE_BG, s.rect, border_radius=8)
        kw = s.rect.w // 2 - 2
        kx = s.rect.x + s.rect.w // 2 + 1 if s.state else s.rect.x + 1
        c = C_TOGGLE_ON if s.state else C_TOGGLE_OFF
        pygame.draw.rect(surface, c, (kx, s.rect.y+2, kw, s.rect.h-4), border_radius=6)
        t = font.render(s.label, True, C_LABEL)
        surface.blit(t, (s.rect.x + (s.rect.w - t.get_width())//2, s.rect.y + s.rect.h + 4))
    def check_click(s, mx, my):
        if s.rect.collidepoint(mx, my): s.state = not s.state; return True
        return False

class TriToggle:
    MODES = ["ВЫКЛ", "АВТО", "ВКЛ"]
    COLORS = [C_TOGGLE_OFF, (180,150,50), C_TOGGLE_ON]
    def __init__(s, x, y, w, h, label, mode=2):
        s.rect = pygame.Rect(x, y, w, h); s.label = label; s.mode = mode
    def draw(s, surface, font):
        pygame.draw.rect(surface, C_TOGGLE_BG, s.rect, border_radius=8)
        kw = s.rect.w // 3 - 2
        kx = s.rect.x + 1 + s.mode * (s.rect.w // 3)
        pygame.draw.rect(surface, s.COLORS[s.mode], (kx, s.rect.y+2, kw, s.rect.h-4), border_radius=6)
        t = font.render(f"{s.label}:{s.MODES[s.mode]}", True, C_LABEL)
        surface.blit(t, (s.rect.x + (s.rect.w - t.get_width())//2, s.rect.y + s.rect.h + 4))
    def check_click(s, mx, my):
        if s.rect.collidepoint(mx, my): s.mode = (s.mode + 1) % 3; return True
        return False

class Slot:
    def __init__(s, x, y, w, h, label, slot_index):
        s.rect = pygame.Rect(x, y, w, h); s.label = label; s.slot_index = slot_index
    def draw(s, surface, font, item=None):
        c = C_SLOT_FULL if item else C_SLOT_EMPTY
        pygame.draw.rect(surface, c, s.rect, border_radius=4)
        pygame.draw.rect(surface, C_SLOT_BORDER, s.rect, 2, border_radius=4)
        if not item: pygame.draw.rect(surface, (10,10,10), (s.rect.x+8, s.rect.y+8, s.rect.w-16, s.rect.h-16))
        t = font.render(item.get_name()[:4] if item else s.label, True, T_TEXT if item else C_LABEL)
        surface.blit(t, t.get_rect(center=s.rect.center))

# UI классы (Программаторы)
class TextBox:
    def __init__(s, x, y, w, h, text, max_chars=6):
        s.rect = pygame.Rect(x, y, w, h); s.text = str(text); s.max_chars = max_chars; s.active = False
    def draw(s, surface, font):
        c = (150,150,180) if s.active else (50,50,60)
        pygame.draw.rect(surface, c, s.rect, border_radius=5)
        pygame.draw.rect(surface, (255,255,255) if s.active else (150,150,150), s.rect, 1, border_radius=5)
        t = font.render(s.text, True, (0,0,0) if s.active else (255,255,255))
        surface.blit(t, t.get_rect(center=s.rect.center))
    def clk(s, mx, my): return s.rect.collidepoint(mx, my)
    def handle_key(s, event):
        if event.key == pygame.K_BACKSPACE:
            s.text = s.text[:-1]
            if s.text in ("", "-"): s.text = "0"
        elif event.unicode == "-":
            if s.text.startswith("-"): s.text = s.text[1:]
            elif s.text == "0": s.text = "-"
            else: s.text = "-" + s.text
        elif event.unicode in "0123456789":
            if s.text == "0": s.text = event.unicode
            elif s.text == "-0": s.text = "-" + event.unicode
            elif len(s.text) < s.max_chars: s.text += event.unicode

class NavButton:
    def __init__(s, x, y, w, h, text, action):
        s.rect = pygame.Rect(x, y, w, h); s.text = text; s.action = action
    def draw(s, surface, font, override_color=None):
        mx, my = pygame.mouse.get_pos()
        c = override_color or ((100,100,120) if s.rect.collidepoint(mx, my) else (70,70,80))
        pygame.draw.rect(surface, c, s.rect, border_radius=5)
        pygame.draw.rect(surface, (200,200,200), s.rect, 1, border_radius=5)
        t = font.render(s.text, True, (255,255,255))
        surface.blit(t, t.get_rect(center=s.rect.center))

# =====================================================
# TFT ОТРИСОВКА
# =====================================================
def _tft_text(surf, text, x, y, size, color):
    f = pygame.font.SysFont("Consolas", size); surf.blit(f.render(text, False, color), (x, y))

def _draw_bar(surf, x, y, w, h, val, mx, ch, cm, cl):
    pygame.draw.rect(surf, T_BORDER, (x,y,w,h), 1)
    if mx <= 0: return
    fw = max(0, min(int((val/mx)*(w-2)), w-2))
    p = val/mx; c = ch if p > 0.5 else (cm if p > 0.2 else cl)
    if fw > 0: pygame.draw.rect(surf, c, (x+1, y+1, fw, h-2))

def _notify_lines(player):
    """До 2 строк журнала уведомлений для главной страницы."""
    now = time.time()
    lmc = getattr(player, "last_manual_cycle_time", 0.0)
    fresh = [n for n in player.notifications
             if (now - n["time"] < 10) or (now - lmc < 10)]
    if not fresh:
        return []
    idx = getattr(player, "current_note_idx", len(fresh) - 1)
    idx = min(max(idx, 0), len(fresh) - 1)
    lines = []
    seen = set()
    for off in range(len(fresh)):
        i = (idx - off) % len(fresh)
        if i in seen:
            continue
        seen.add(i)
        n = fresh[i]
        lines.append((n["text"][:24], _notify_color(n.get("type", "info"))))
        if len(lines) >= 2:
            break
    return lines

def _completed_achievements(player):
    return [(code, ACHIEVEMENTS[code]["name"])
            for code in sorted(player.achievements_unlocked)
            if code in ACHIEVEMENTS]

def draw_tft(tft, player, evt_text, evt_color, evt_time, power_on, page,
             browse_entry=None, browse_num=0, browse_total=0,
             menu_sub_idx=0, menu_in_detail=False, menu_detail_idx=0,
             task_detail=None, ach_detail=None, surrender_confirm=None):
    if not power_on: tft.fill(T_BG); return
    if player.needs_registration:
        tft.fill(T_BG)
        pygame.draw.rect(tft, T_BORDER, (0, 0, TFT_W, TFT_H), 2)
        _tft_text(tft, "РЕГИСТРАЦИЯ", 30, 30, 12, T_BORDER)
        _tft_text(tft, "ПОДКЛЮЧИТЕ", 25, 55, 10, T_TEXT)
        _tft_text(tft, "К ПК / EEPROM", 20, 70, 10, T_TEXT)
        return
    if player.admit_pending:
        tft.fill(T_BG)
        pygame.draw.rect(tft, T_DEATH, (0,0,TFT_W,TFT_H), 3)
        _tft_text(tft, "ОЖИДАНИЕ", 40, 30, 14, T_DEATH)
        _tft_text(tft, "ДОПУСКА", 45, 55, 14, T_DEATH)
        _tft_text(tft, "ДОПУСК В ИГРУ", 20, 80, 11, T_BORDER)
        return
    if player.is_zombie:
        tft.fill((40, 0, 0))
        pygame.draw.rect(tft, T_DEATH, (0, 0, TFT_W, TFT_H), 3)
        _tft_text(tft, "ВЫ ЗОМБИ", 40, 40, 14, T_DEATH)
        if player.player_name:
            _tft_text(tft, player.player_name[:12], 30, 60, 9, T_BORDER)
        if surrender_confirm:
            _tft_text(tft, "ОК ЕЩЁ РАЗ!", 20, 85, 10, T_DEATH)
        else:
            _tft_text(tft, "СДАТЬСЯ: ОК", 25, 85, 10, T_TEXT)
        return
    tft.fill(T_BG)
    if page == 0:
        project = PROJECT_THEMES.get(getattr(player, "project_id", 0), PROJECT_THEMES[0])
        title = f"{project['name']} PDA"
        _tft_text(tft, title, max(2, 80-len(title)*3), 2, 9, T_BORDER)
        pygame.draw.line(tft, T_BORDER, (0,13), (160,13))
        _tft_text(tft, "HP", 3, 17, 9, T_TEXT)
        _draw_bar(tft, 20, 16, 135, 12, player.health, player.max_health, T_HP_HI, T_HP_MID, T_HP_LO)
        _tft_text(tft, "RAD", 1, 33, 9, T_RAD)
        max_rad = getattr(player, "max_rad", 1000)
        _draw_bar(tft, 22, 32, 133, 12, player.radiation, max_rad, T_RAD, T_RAD, T_RAD)
        _tft_text(tft, f"LV{player.level}", 3, 50, 9, T_XP)
        _tft_text(tft, f"{player.money}RUB", 70, 50, 8, T_MONEY)
        _tft_text(tft, f"РГ:{player.get_rank_title()[:6]}", 3, 62, 8, T_BORDER)
        cons = player.slots[1]
        if cons:
            _tft_text(tft, f"[{cons.get_name()[:8]}] ОК", 3, 74, 8, T_MONEY)
        for li, (txt, ncol) in enumerate(_notify_lines(player)):
            _tft_text(tft, txt, 3, 88 + li * 10, 8, ncol)
        if player.emission_timer == 0 and player.emission_duration > 0:
            if int(time.time()*2)%2: pygame.draw.rect(tft, T_HP_LO, (0,0,160,128), 2)
    elif page == 1:
        _tft_text(tft, "ИНВЕНТАРЬ", 50, 2, 9, T_BORDER)
        pygame.draw.line(tft, T_BORDER, (0,13), (160,13))
        _tft_text(tft, f"БАЛАНС: {player.money} RUB", 5, 17, 9, T_MONEY)
        pygame.draw.line(tft, T_BORDER, (0,29), (160,29))
        inv = [(0,"БРОНЯ"),(2,"АРТ 1"),(3,"АРТ 2"),(4,"АРТ 3")]
        for ri, (sid, pfx) in enumerate(inv):
            y = 35 + ri*16; item = player.slots[sid]
            st = item.get_name() if item else "[---]"; col = T_HP_HI if item else T_BORDER
            if getattr(player,"selected_row",0) == ri:
                pygame.draw.rect(tft, (50,50,50), (2,y-1,156,13))
                _tft_text(tft, ">", 2, y, 9, T_TEXT)
                _tft_text(tft, f"{pfx}: {st}", 12, y, 9, col)
            else: _tft_text(tft, f"{pfx}: {st}", 10, y, 9, col)
    elif page == 2:
        if not menu_in_detail:
            _tft_text(tft, "МЕНЮ", 65, 2, 9, T_BORDER)
            pygame.draw.line(tft, T_BORDER, (0, 13), (160, 13))
            for ri, name in enumerate(MENU_ITEMS):
                y = 22 + ri * 16
                mark = ">" if ri == menu_sub_idx else " "
                col = T_HP_HI if ri == menu_sub_idx else T_TEXT
                _tft_text(tft, f"{mark}{name[:15]}", 5, y, 8, col)
            _tft_text(tft, "ОК=ВОЙТИ", 3, 88, 8, T_BORDER)
        elif menu_sub_idx == 0:
            _tft_text(tft, "СОПРОТИВЛ.", 40, 2, 9, T_BORDER)
            pygame.draw.line(tft, T_BORDER, (0, 13), (160, 13))
            equip_res = {i: 0.0 for i in range(8)}
            for sl in player.slots:
                if sl and sl.modifiers:
                    for k, v in sl.modifiers.items():
                        if k in equip_res:
                            equip_res[k] += v
            project = PROJECT_THEMES.get(getattr(player, "project_id", 0), PROJECT_THEMES[0])
            names = project["dmg_names"]
            row = 0
            for i in range(8):
                if not names[i]:
                    continue
                y = 18 + row * 12
                inn = int(player.innate_resistance.get(i, 0))
                eq = int(equip_res[i])
                tot = min(100, inn + eq)
                _tft_text(tft, f"{names[i][:7]}:", 3, y, 8, T_TEXT)
                if inn > 0:
                    _tft_text(tft, f"{tot}%({inn})", 100, y, 8, T_HP_HI if tot > 0 else T_BORDER)
                else:
                    _tft_text(tft, f"{tot}%", 120, y, 8, T_HP_HI if tot > 0 else T_BORDER)
                row += 1
            _tft_text(tft, "ОК=НАЗАД", 3, 88, 8, T_BORDER)
        elif menu_sub_idx == 1:
            _tft_text(tft, "КВЕСТЫ", 55, 2, 9, T_BORDER)
            pygame.draw.line(tft, T_BORDER, (0, 13), (160, 13))
            tasks = player.get_active_tasks()
            if not tasks:
                _tft_text(tft, "НЕТ АКТИВНЫХ", 20, 40, 9, T_BORDER)
            else:
                sel = min(menu_detail_idx, len(tasks) - 1)
                for ri, task in enumerate(tasks[:4]):
                    y = 20 + ri * 14
                    mark = ">" if ri == sel else " "
                    _tft_text(tft, f"{mark}{task.title[:14]}", 3, y, 8,
                              T_HP_HI if ri == sel else T_TEXT)
                if task_detail:
                    _tft_text(tft, task_detail[:26], 3, 82, 8, T_HP_MID)
                else:
                    _tft_text(tft, "ОК=ОПИСАНИЕ", 3, 88, 8, T_BORDER)
                _tft_text(tft, f"({len(tasks)})", 130, 2, 8, T_BORDER)
        elif menu_sub_idx == 2:
            _tft_text(tft, "ДОСТИЖЕНИЯ", 40, 2, 9, T_BORDER)
            pygame.draw.line(tft, T_BORDER, (0, 13), (160, 13))
            achs = _completed_achievements(player)
            if not achs:
                _tft_text(tft, "ПОКА НЕТ", 35, 40, 9, T_BORDER)
            else:
                sel = min(menu_detail_idx, len(achs) - 1)
                for ri, (_, name) in enumerate(achs[:5]):
                    y = 18 + ri * 13
                    mark = ">" if ri == sel else " "
                    _tft_text(tft, f"{mark}{name[:14]}", 3, y, 8,
                              T_HP_HI if ri == sel else T_XP)
                if ach_detail:
                    _tft_text(tft, ach_detail[:26], 3, 88, 8, T_HP_MID)
                else:
                    _tft_text(tft, "ОК=ДЕТАЛИ", 3, 88, 8, T_BORDER)
        elif menu_sub_idx == 3:
            _tft_text(tft, "СДАТЬСЯ", 50, 2, 9, T_DEATH)
            pygame.draw.line(tft, T_BORDER, (0, 13), (160, 13))
            _tft_text(tft, "Потеря связи.", 5, 24, 8, T_TEXT)
            _tft_text(tft, "Нужно воскр.", 5, 36, 8, T_BORDER)
            if surrender_confirm:
                _tft_text(tft, "ОК ЕЩЁ РАЗ!", 5, 58, 9, T_DEATH)
            else:
                _tft_text(tft, "ОК=ПОДТВЕРД.", 3, 88, 8, T_BORDER)
    pygame.draw.line(tft, T_BORDER, (0,115), (160,115))
    if evt_text and (time.time()-evt_time < 3.5):
        _tft_text(tft, evt_text[:18], 3, 118, 8, evt_color)
    elif browse_entry:
        idx_str = f"{browse_num}/{browse_total} "
        _tft_text(tft, idx_str + browse_entry["text"][:15], 3, 118, 8, browse_entry["color"])
    if player.emission_timer == 0 and player.emission_duration > 0:
        _tft_text(tft, "ВЫБРОС!", 115, 118, 9, T_HP_LO)
    if player.is_dead:
        pygame.draw.rect(tft, T_DEATH, (0, 0, TFT_W, TFT_H), 2)
        _tft_text(tft, "СВЯЗЬ ПОТЕРЯНА", 18, 2, 8, T_DEATH)
        _tft_text(tft, "ВОСКРЕШЕНИЕ", 35, 11, 8, T_BORDER)

# =====================================================
# PDA ПАНЕЛЬ (ПОЛНЫЙ КОРПУС)
# =====================================================
class PDAPanel:
    def __init__(self, x, y, port, save_file):
        self.ox, self.oy = x, y
        self.port = port
        self.player = Player(save_file=save_file)
        self.player.selected_row = 0
        self.player.last_manual_cycle_time = 0.0
        self.current_page = 0
        self.last_interaction = time.time()
        self.screen_dimmed = False
        self.tft = pygame.Surface((TFT_W, TFT_H))
        self.evt_text = ""; self.evt_color = T_TEXT; self.evt_time = 0
        self.evt_history = []  # Последние 5 событий [{"text":...,"color":...}]
        self.evt_browse_idx = -1  # -1 = авто (последнее), 0..4 = ручной просмотр
        self.evt_browse_time = 0
        self.last_tick = time.time()
        self._was_powered = False
        self.vibrate_until = 0.0
        self.led_red_flash_until = 0.0
        self.detector_alert_until = 0.0
        self.menu_sub_idx = 0
        self.menu_in_detail = False
        self.menu_detail_idx = 0
        self.task_detail = ""
        self.task_detail_time = 0.0
        self.ach_detail = ""
        self.ach_detail_time = 0.0
        self.surrender_confirm = False
        self.surrender_confirm_time = 0.0
        self.q = queue.Queue()
        # Размеры корпуса (компактные)
        self.BW, self.BH = 350, 340
        scr_x, scr_y = x + 15, y + 15
        self.scr_x, self.scr_y = scr_x, scr_y
        # Тумблеры (справа от экрана)
        tx = scr_x + SCREEN_W + 10
        self.toggle_power = Toggle(tx, scr_y + 155, 42, 20, "ПИТАН", True)
        self.toggle_light = TriToggle(tx, scr_y + 120, 42, 20, "СВЕТ", 2)
        self.toggle_sound = Toggle(tx, scr_y + 85, 42, 20, "ЗВУК", True)
        self.toggle_vibro = Toggle(tx, scr_y + 65, 42, 20, "ВИБРО", True)
        self.toggle_radio = Toggle(tx, scr_y + 50, 42, 20, "РАДИО", False)
        self.toggles = [self.toggle_power, self.toggle_light, self.toggle_sound, self.toggle_vibro, self.toggle_radio]
        # Кнопки
        by = scr_y + SCREEN_H + 10
        self.buttons = [
            Button(x + 10, by, 50, 26, "ВНИЗ", "btn_down"),
            Button(x + 70, by, 55, 26, "ВПРАВО", "btn_right"),
            Button(x + 135, by, 45, 26, "ОК", "btn_ok"),
        ]
        # Слоты
        sy = by + 35
        self.slots_ui = [
            Slot(x + 8, sy, 60, 38, "БРОНЯ", 0),
            Slot(x + 78, sy, 48, 35, "АРТ1", 2),
            Slot(x + 133, sy, 48, 35, "АРТ2", 3),
            Slot(x + 188, sy, 48, 35, "АРТ3", 4),
        ]
        # Слот расходника (справа от экрана, рядом с тумблерами)
        self.slot_consumable = Slot(tx, scr_y + 15, 45, 30, "РАСХ", 1)
        # LED
        self.led_pos = (x + 260, scr_y + SCREEN_H + 5)
        # UDP
        threading.Thread(target=self._udp, daemon=True).start()

    def _udp(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try: s.bind(('127.0.0.1', self.port))
        except: return
        s.settimeout(1.0)
        while True:
            try:
                data, addr = s.recvfrom(1024)
                if len(data) == Packet.SIZE: self.q.put((Packet.unpack(data), addr))
            except: pass

    def _trigger_vibro(self, ms: int):
        if self.toggle_vibro.state:
            self.vibrate_until = time.time() + ms / 1000.0

    def _feedback_for_event(self, evt):
        """Звук и вибро по типу события."""
        if not evt:
            return
        et = evt.get("type", "")
        if evt.get("notify"):
            et = evt.get("type", et)
        if et == "achievement":
            if self.toggle_sound.state:
                _play_tone(880, 140)
                _play_tone(1100, 100)
        elif et in ("rank_ready", "rank_confirmed"):
            if self.toggle_sound.state:
                _play_tone(660, 160)
                _play_tone(990, 120)
        elif et == "damage":
            self.led_red_flash_until = time.time() + 0.4
        if et in VIBRO_EVENTS:
            self._trigger_vibro(VIBRO_MS.get(et, 200))

    def set_event(self, evt):
        if evt is None: return
        if isinstance(evt, list):
            for e in evt:
                self.set_event(e)
            return
        self._feedback_for_event(evt)
        self.evt_text = evt.get("text", "")
        colors = {"damage":T_HP_LO,"death":T_HP_LO,"zombie":T_HP_LO,"heal":T_HP_HI,"revive":T_HP_HI,"radiation":T_RAD,
                  "antirad":T_RAD,"money":T_MONEY,"xp":T_XP,"error":T_HP_MID,"info":T_BORDER,
                  "achievement":T_XP,"rank_ready":(255,220,80),"rank_confirmed":T_MONEY,"level_up":T_HP_HI}
        self.evt_color = colors.get(evt.get("type",""), T_TEXT)
        if evt.get("notify"):
            self.evt_color = _notify_color(evt.get("type", "info"))
        self.evt_time = time.time()
        # История событий (макс 5)
        self.evt_history.append({"text": self.evt_text, "color": self.evt_color})
        if len(self.evt_history) > 5: self.evt_history.pop(0)
        self.evt_browse_idx = -1  # сброс на авто (последнее)
        # Глобальный лог
        global event_log
        ts = time.strftime("%H:%M:%S")
        src = f"PDA:{self.port}"
        event_log.append({"time": ts, "src": src, "text": self.evt_text, "color": self.evt_color})
        if len(event_log) > MAX_LOG: event_log.pop(0)

    def _on_anomaly_signal(self):
        """Детектор: ур.25+ и пакет аномалии рядом (ESP-NOW) — мигание LED."""
        if self.player.level >= 25:
            self.detector_alert_until = time.time() + 3.0

    def tick(self):
        t = time.time()
        powered = self.toggle_power.state
        if powered and not self._was_powered:
            self.player.on_power_on()
        self._was_powered = powered
        if t - self.last_tick >= 1.0:
            self.last_tick = t
            if powered and not self.player.is_system_locked() and not self.player.is_dead and not self.player.is_zombie:
                r = self.player.tick()
                if r: self.set_event(r)
                rr = self.player._check_rank_eligibility()
                if rr: self.set_event(rr)
        # Dimming
        if self.toggle_power.state and self.toggle_light.mode == 1:
            self.screen_dimmed = (t - self.last_interaction > 10)
        elif self.toggle_light.mode == 0: self.screen_dimmed = True
        else: self.screen_dimmed = False
        # Net
        while not self.q.empty():
            pkt, addr = self.q.get()
            if not self.toggle_power.state:
                continue
            if self.player.is_system_locked():
                if pkt.msg_type == Msg.COMMAND and pkt.val1 == CmdSub.ADMIT and self.player.admit_pending:
                    self.set_event(self.player.grant_session_admit())
                continue
            if self.player.is_zombie:
                continue
            if self.player.is_dead:
                if pkt.msg_type == Msg.COMMAND and pkt.val1 == CmdSub.REVIVE:
                    self.set_event(self.player.grant_revive())
                continue
            if pkt.msg_type == Msg.DAMAGE:
                if pkt.emitter == Emitter.ANOMALY:
                    self._on_anomaly_signal()
                if pkt.val3 != 0:
                    # Новый формат: val1=hp_dmg, val3=dmg_mask (val2=0, бит 5 зарезервирован)
                    self.set_event(self.player.apply_damage_multi(
                        pkt.val1, pkt.val3, source_id=str(addr)))
                else:
                    # Старый формат: val1=dmg, val2=DmgType
                    self.set_event(self.player.apply_damage(pkt.val1, pkt.val2, source_id=str(addr)))




            elif pkt.msg_type == Msg.HEAL:
                self.set_event(self.player.apply_heal(pkt.val1))
            elif pkt.msg_type == Msg.RADIATION:
                if pkt.emitter == Emitter.ANOMALY:
                    self._on_anomaly_signal()
                self.set_event(self.player.apply_radiation(pkt.val1))
            elif pkt.msg_type == Msg.SAFE_ZONE:
                self.player.last_safe_zone_time = time.time()
                if pkt.val3 == SZ_PROT_FLAG:
                    # Пакет защиты зоны: val1=type(0-7), val2=pct
                    self.player.apply_zone_prot(pkt.val1, pkt.val2)
                else:
                    # Хил-тик: val1=HP, val2=RAD
                    if pkt.val1 > 0:
                        self.set_event(self.player.apply_heal(pkt.val1))
                    if pkt.val2 > 0:
                        self.set_event(self.player.apply_antirad(pkt.val2))
            elif pkt.msg_type == Msg.EMISSION:
                self.player.emission_timer = pkt.val1; self.player.emission_duration = pkt.val2
            elif pkt.msg_type == Msg.COMMAND:

                if pkt.val1 == CmdSub.KILL: self.set_event(self.player.apply_damage(9999))
                elif pkt.val1 == CmdSub.WIPE_DATA:
                    from game_logic import STAT_SCALE
                    self.player.health = STAT_SCALE; self.player.max_health = STAT_SCALE
                    self.player.radiation = 0; self.player.max_rad = STAT_SCALE
                    self.player.money = 1000
                    self.player.xp = 0; self.player.level = 1
                    self.player.rank = 0
                    self.player.achievements_unlocked = set()
                    self.player.active_tasks = []
                    self.player.registered = False
                    self.player.player_name = ""
                    self.player.is_zombie = False
                    self.player.cheat_shield_count = 0
                    self.player.quests_completed = 0
                    self.player.anomaly_sources = set()
                    self.player.rank_ready_notified = set()
                    self.player._had_first_admit = False
                    self.player.is_dead = False; self.player.death_counter = 0
                    self.player.on_power_on()
                    self.player.slots = [None]*5
                    self.player.innate_resistance = {i:0.0 for i in range(8)}
                    self.player.damage_taken_accumulator = {i:0.0 for i in range(8)}
                    self.player.save_state()
                    self.set_event({"type":"info","text":"СБРОС ДО ЗАВОДСКИХ"})

    def handle_click(self, mx, my):
        global selected_chip_idx, world_chips
        self.last_interaction = time.time()
        if self.player.is_zombie:
            for btn in self.buttons:
                if btn.check_hover(mx, my) and btn.click() == "btn_ok":
                    if self.surrender_confirm and time.time() - self.surrender_confirm_time < 8:
                        self.set_event(self.player.surrender())
                        self.surrender_confirm = False
                    else:
                        self.surrender_confirm = True
                        self.surrender_confirm_time = time.time()
            return
        if self.screen_dimmed and self.toggle_light.mode == 1 and self.toggle_power.state:
            self.screen_dimmed = False; return
        for tg in self.toggles:
            if tg.check_click(mx, my):
                if isinstance(tg, TriToggle): self.set_event({"type":"info","text":f"{tg.label}:{tg.MODES[tg.mode]}"})
                else: self.set_event({"type":"info","text":f"{tg.label}: {'ВКЛ' if tg.state else 'ВЫКЛ'}"})
        INV_MAP = [0, 2, 3, 4]
        for btn in self.buttons:
            if btn.check_hover(mx, my):
                a = btn.click()
                if a == "btn_down":
                    if self.current_page == 0:
                        if self.player.notifications:
                            self.player.cycle_notifications()
                            self.player.last_manual_cycle_time = time.time()
                        if self.evt_history:
                            if self.evt_browse_idx == -1:
                                self.evt_browse_idx = len(self.evt_history) - 2
                            else:
                                self.evt_browse_idx -= 1
                            if self.evt_browse_idx < 0: self.evt_browse_idx = len(self.evt_history) - 1
                            self.evt_browse_time = time.time()
                    elif self.current_page == 1:
                        self.player.selected_row = (self.player.selected_row + 1) % 4
                    elif self.current_page == 2:
                        if not self.menu_in_detail:
                            self.menu_sub_idx = (self.menu_sub_idx + 1) % len(MENU_ITEMS)
                        elif self.menu_sub_idx == 1:
                            tasks = self.player.get_active_tasks()
                            if tasks:
                                self.menu_detail_idx = (self.menu_detail_idx + 1) % len(tasks)
                                self.task_detail = ""
                        elif self.menu_sub_idx == 2:
                            achs = _completed_achievements(self.player)
                            if achs:
                                self.menu_detail_idx = (self.menu_detail_idx + 1) % len(achs)
                                self.ach_detail = ""
                        elif self.menu_sub_idx == 3:
                            self.surrender_confirm = False
                elif a == "btn_right":
                    self.current_page = (self.current_page + 1) % NUM_PAGES
                    self.menu_in_detail = False
                    self.menu_detail_idx = 0
                    self.task_detail = ""
                    self.ach_detail = ""
                    self.surrender_confirm = False
                elif a == "btn_ok":
                    if self.current_page == 0:
                        c = self.player.slots[1]
                        if c:
                            self.set_event(self.player.use_consumable(c))
                            if c.used: self.player.slots[1] = None
                        else: self.set_event({"type":"info","text":"НЕТ РАСХОДНИКА"})
                    elif self.current_page == 1:
                        rs = INV_MAP[self.player.selected_row]
                        item = self.player.slots[rs]
                        if item: self.set_event(self.player.remove_artifact(rs))
                        else: self.set_event({"type":"info","text":"СЛОТ ПУСТ"})
                    elif self.current_page == 2:
                        if not self.menu_in_detail:
                            self.menu_in_detail = True
                            self.menu_detail_idx = 0
                            self.task_detail = ""
                            self.ach_detail = ""
                            self.surrender_confirm = False
                        elif self.menu_sub_idx == 0:
                            self.menu_in_detail = False
                        elif self.menu_sub_idx == 1:
                            tasks = self.player.get_active_tasks()
                            if tasks:
                                sel = min(self.menu_detail_idx, len(tasks) - 1)
                                t = tasks[sel]
                                if self.task_detail and time.time() - self.task_detail_time < 8:
                                    self.set_event(self.player.complete_quest(t.id))
                                    self.task_detail = ""
                                else:
                                    self.task_detail = t.short_desc or t.title
                                    self.task_detail_time = time.time()
                            else:
                                self.set_event({"type":"info","text":"НЕТ ЗАДАНИЙ"})
                        elif self.menu_sub_idx == 2:
                            achs = _completed_achievements(self.player)
                            if achs:
                                sel = min(self.menu_detail_idx, len(achs) - 1)
                                code, name = achs[sel]
                                ach = ACHIEVEMENTS[code]
                                if self.ach_detail and time.time() - self.ach_detail_time < 8:
                                    self.menu_in_detail = False
                                    self.ach_detail = ""
                                else:
                                    self.ach_detail = f"+{ach['xp']}XP +{ach['rub']}R"
                                    self.ach_detail_time = time.time()
                            else:
                                self.set_event({"type":"info","text":"НЕТ ДОСТИЖ."})
                        elif self.menu_sub_idx == 3:
                            if self.surrender_confirm and time.time() - self.surrender_confirm_time < 8:
                                self.set_event(self.player.surrender())
                                self.menu_in_detail = False
                                self.surrender_confirm = False
                            else:
                                self.surrender_confirm = True
                                self.surrender_confirm_time = time.time()
        # Вставка чипов из world_chips
        all_slots = self.slots_ui + [self.slot_consumable]
        for sl in all_slots:
            if sl.rect.collidepoint(mx, my):
                if selected_chip_idx >= 0 and selected_chip_idx < len(world_chips):
                    chip = world_chips[selected_chip_idx]
                    si = sl.slot_index
                    if si == 1:  # расходник
                        if self.player.slots[1] is None:
                            self.player.slots[1] = chip
                            world_chips.pop(selected_chip_idx); selected_chip_idx = -1
                            self.set_event({"type":"info","text":"ЧИП ВСТАВЛЕН"})
                        else: self.set_event({"type":"error","text":"СЛОТ ЗАНЯТ!"})
                    elif si == 0:  # броня
                        if chip.item_type == ItemChip.TYPE_ARMOR or chip.item_type == 3:
                            r = self.player.insert_artifact(si, chip)
                            if r and r.get("type") != "error":
                                world_chips.pop(selected_chip_idx); selected_chip_idx = -1
                            self.set_event(r)
                        else: self.set_event({"type":"error","text":"ТОЛЬКО БРОНЯ!"})
                    else:  # арты
                        r = self.player.insert_artifact(si, chip)
                        if r and r.get("type") != "error":
                            world_chips.pop(selected_chip_idx); selected_chip_idx = -1
                        self.set_event(r)
                elif self.player.slots[sl.slot_index]:
                    if sl.slot_index == 1:
                        self.set_event(self.player.use_consumable(self.player.slots[1]))
                        self.player.slots[1] = None
                    else:
                        # Броня/Арт — извлечь и вернуть в список чипов
                        extracted = self.player.slots[sl.slot_index]
                        self.set_event(self.player.remove_artifact(sl.slot_index))
                        if extracted:
                            extracted.used = False  # сброс флага, можно использовать повторно
                            world_chips.append(extracted)
                else:
                    self.set_event({"type":"info","text":f"СЛОТ {sl.label} ПУСТ"})

    def draw(self, screen, font_sm, font_md):
        x, y = self.ox, self.oy
        # Подготовка данных просмотра истории
        be = None; bn = 0; bt = len(self.evt_history)
        if self.evt_browse_idx >= 0 and self.evt_history and (time.time() - self.evt_browse_time < 10):
            idx = min(self.evt_browse_idx, len(self.evt_history)-1)
            be = self.evt_history[idx]
            bn = idx + 1
        # Корпус
        pygame.draw.rect(screen, (10,10,12), (x+3, y+3, self.BW, self.BH), border_radius=10)
        pygame.draw.rect(screen, C_BODY, (x, y, self.BW, self.BH), border_radius=8)
        pygame.draw.rect(screen, C_BODY_LIGHT, (x+2, y+2, self.BW-4, self.BH-4), 1, border_radius=7)
        # Шурупы
        for sx, sy in [(x+10,y+10),(x+self.BW-10,y+10),(x+10,y+self.BH-10),(x+self.BW-10,y+self.BH-10)]:
            pygame.draw.circle(screen, C_SCREW, (sx,sy), 3)
        # Безель
        pygame.draw.rect(screen, C_SCREEN_BEZEL, (self.scr_x-4, self.scr_y-4, SCREEN_W+8, SCREEN_H+8), border_radius=3)
        # TFT
        td = self.task_detail if self.current_page == 2 and self.menu_in_detail and self.menu_sub_idx == 1 and (time.time() - self.task_detail_time < 8) else None
        ad = self.ach_detail if self.current_page == 2 and self.menu_in_detail and self.menu_sub_idx == 2 and (time.time() - self.ach_detail_time < 8) else None
        sd = None
        if self.player.is_zombie and self.surrender_confirm and (time.time() - self.surrender_confirm_time < 8):
            sd = True
        elif (self.current_page == 2 and self.menu_in_detail
              and self.menu_sub_idx == 3 and (time.time() - self.surrender_confirm_time < 8)):
            sd = self.surrender_confirm
        draw_tft(self.tft, self.player, self.evt_text, self.evt_color, self.evt_time, self.toggle_power.state,
                 self.current_page, be, bn, bt,
                 self.menu_sub_idx, self.menu_in_detail, self.menu_detail_idx, td, ad, sd)
        scaled = pygame.transform.scale(self.tft, (SCREEN_W, SCREEN_H))
        if self.screen_dimmed and self.toggle_power.state:
            scaled.fill((40,40,40), special_flags=pygame.BLEND_MULT)
        if time.time() < self.vibrate_until and self.toggle_vibro.state:
            ox = int((time.time() * 40) % 2) * 2 - 1
            screen.blit(scaled, (self.scr_x + ox, self.scr_y))
        else:
            screen.blit(scaled, (self.scr_x, self.scr_y))
        # LED: зелёный = ЗЗ; красный = вспышка урона или мигание детектора
        now = time.time()
        gz_on = self.player.is_in_safe_zone and self.toggle_power.state
        lc = (0, 255, 0) if gz_on else (20, 40, 20)
        red_pos = (self.led_pos[0], self.led_pos[1] - 14)
        detector_blink = (self.player.level >= 25 and now < self.detector_alert_until
                          and int(now * 4) % 2 == 0)
        red_flash = now < self.led_red_flash_until
        if red_flash or detector_blink:
            rc = (255, 40, 40)
        else:
            rc = (40, 15, 15)
        pygame.draw.circle(screen, rc, red_pos, 5)
        pygame.draw.circle(screen, lc, self.led_pos, 5)
        screen.blit(font_sm.render("DMG", True, C_LABEL), (red_pos[0] + 8, red_pos[1] - 5))
        screen.blit(font_sm.render("SZ", True, C_LABEL), (self.led_pos[0] + 8, self.led_pos[1] - 5))
        # Тумблеры
        for tg in self.toggles: tg.draw(screen, font_sm)
        # Кнопки
        mx, my = pygame.mouse.get_pos()
        for btn in self.buttons: btn.check_hover(mx, my); btn.draw(screen, font_sm)
        # Слоты
        for sl in self.slots_ui: sl.draw(screen, font_sm, self.player.slots[sl.slot_index])
        self.slot_consumable.draw(screen, font_sm, self.player.slots[1])
        # Порт
        screen.blit(font_sm.render(f"UDP:{self.port}", True, C_LABEL), (x+5, y+self.BH-15))


# =====================================================
# ПРОГРАММАТОР ЧИПОВ (ПОЛНЫЙ)
# =====================================================
class ChipProgrammer:
    TYPES = ["АДМИНКА (Мастер)", "РАСХОДНИКИ", "АРТЕФАКТЫ", "БРОНЯ"]
    SUBTYPES = {
        0: ["ВОСКРЕШЕНИЕ","ДОПУСК В ИГРУ","УПРАВЛЕНИЕ ДЕНЬГАМИ","ПОДТВ. РАНГА"],
        1: ["МГНОВ. ЛЕЧЕНИЕ","АНТИРАДИН","ВИНЧА (РЕГЕН)","ПСИ-БЛОК","РЕМОНТ БРОНИ","ЗАРЯДКА АРТА","КВЕСТ","СДАТЬ КВЕСТ"],
        2: ["УНИВЕРСАЛЬНЫЙ ЧИП"], 3: ["УНИВЕРСАЛЬНЫЙ ЧИП"]
    }
    # 8 типов урона (0-7), совпадают с SubType в протоколе
    DMG8 = [("ВЗРЫВ",1,2),("КРОВЬ",3,4),("ТЕРМО",5,6),("ЭЛЕКТРО",7,8),("ХИМИЯ",9,10),
            ("РАДИАЦИЯ",11,12),("ПСИ",13,14),("ГРАВИ",15,16)]

    def __init__(self, x, y, w, h):
        self.x, self.y, self.w, self.h = x, y, w, h
        self.cfg = {"t": 1, "st": 0}
        self.boxes = [TextBox(0, 0, 45, 24, "0", 5) for _ in range(18)]  # 0=реген, 1-16=пары защ/урон, 17=интервал
        self.boxes[17].text = "1"
        self.b_uses = TextBox(0, 0, 45, 24, "1", 5)
        self.b_tp = NavButton(x+10, y+40, 30, 24, "<", "tp")
        self.b_tn = NavButton(x+w-40, y+40, 30, 24, ">", "tn")
        self.b_stp = NavButton(x+10, y+70, 30, 24, "<", "stp")
        self.b_stn = NavButton(x+w-40, y+70, 30, 24, ">", "stn")
        self.b_flash = NavButton(x+w//2-60, y+h-40, 120, 30, "ПРОШИТЬ ЧИП", "flash")
        self.flash_time = 0

    def handle_click(self, mx, my):
        global active_textbox, world_chips
        if not pygame.Rect(self.x, self.y, self.w, self.h).collidepoint(mx, my): return
        for bx in self.boxes + [self.b_uses]:
            if bx.rect.collidepoint(mx, my): active_textbox = bx; bx.active = True
            else: bx.active = False
        if self.b_tp.rect.collidepoint(mx, my):
            self.cfg["t"] = (self.cfg["t"]-1) % 4; self.cfg["st"] = 0; self._reset_boxes()
        if self.b_tn.rect.collidepoint(mx, my):
            self.cfg["t"] = (self.cfg["t"]+1) % 4; self.cfg["st"] = 0; self._reset_boxes()
        subs = self.SUBTYPES[self.cfg["t"]]
        if self.b_stp.rect.collidepoint(mx, my): self.cfg["st"] = (self.cfg["st"]-1) % len(subs)
        if self.b_stn.rect.collidepoint(mx, my): self.cfg["st"] = (self.cfg["st"]+1) % len(subs)
        if self.b_flash.rect.collidepoint(mx, my):
            self.flash_time = time.time()
            chip = self._make_chip()
            if chip: world_chips.append(chip)

    def _reset_boxes(self):
        for b in self.boxes: b.text = "0"
        self.boxes[17].text = "1"; self.b_uses.text = "1"

    def _gi(self, idx):
        try: return int(self.boxes[idx].text) if self.boxes[idx].text not in ("","-") else 0
        except: return 0

    def _make_chip(self):
        t = self.cfg["t"]; st = self.cfg["st"]
        uses = int(self.b_uses.text) if self.b_uses.text not in ("","-") else 1
        if t == 0:  # Админка → всё через TYPE_COMMAND
            if st == 0: return ItemChip(ItemChip.TYPE_COMMAND, ItemChip.CMD_REVIVE, uses=uses)
            elif st == 1: return ItemChip(ItemChip.TYPE_COMMAND, ItemChip.CMD_ADMIT, uses=uses)
            elif st == 2: return ItemChip(ItemChip.TYPE_COMMAND, ItemChip.CMD_MONEY, modifiers={"amount": self._gi(0)}, uses=uses)
            elif st == 3: return ItemChip(ItemChip.TYPE_COMMAND, ItemChip.CMD_SET_PROJECT, modifiers={"pid": self._gi(0)}, uses=uses)
            elif st == 4: return ItemChip(ItemChip.TYPE_COMMAND, 0, modifiers={"rank_confirm": True}, uses=uses)
        elif t == 1:  # Расходники
            if st == 0: return ItemChip(ItemChip.TYPE_MEDKIT, self._gi(0), uses=uses)
            elif st == 1: return ItemChip(ItemChip.TYPE_ANTIRAD, self._gi(0), uses=uses)
            elif st == 2: return ItemChip(ItemChip.TYPE_MEDKIT, self._gi(0), uses=uses)
            elif st == 3: return ItemChip(ItemChip.TYPE_RESISTANCE, self._gi(0), modifiers={6: self._gi(0)}, uses=uses)
            elif st == 4: return ItemChip(ItemChip.TYPE_MEDKIT, self._gi(0), uses=uses)
            elif st == 5: return ItemChip(ItemChip.TYPE_MEDKIT, self._gi(0), uses=uses)
            elif st == 6:
                qid = f"q{self._gi(0) or 1}"
                rub = self._gi(1) or 300
                return ItemChip(ItemChip.TYPE_CONSUMABLE, 0, modifiers={
                    "quest": True, "quest_id": qid,
                    "title": f"Задание {qid}",
                    "short_desc": "Выполнить поручение мастера",
                    "rub_reward": rub,
                }, uses=uses)
            elif st == 7:
                qid = f"q{self._gi(0) or 1}"
                return ItemChip(ItemChip.TYPE_CONSUMABLE, 0, modifiers={
                    "quest": True, "quest_id": qid, "complete": True,
                }, uses=uses)
        elif t == 2:  # Артефакты
            mods = {}
            for i, (name, ri, di) in enumerate(self.DMG8):
                rv = self._gi(ri); dv = self._gi(di)
                if rv != 0: mods[i] = rv
            regen = self._gi(0)
            return ItemChip(ItemChip.TYPE_ARTIFACT, regen, modifiers=mods, uses=uses)
        elif t == 3:  # Броня
            mods = {}
            for i, (name, ri, _) in enumerate(self.DMG8):
                rv = self._gi(ri)
                if rv != 0: mods[i] = rv
            hp_bonus = self._gi(0)
            return ItemChip(ItemChip.TYPE_ARMOR, hp_bonus, modifiers=mods, uses=uses)
        return ItemChip(0, 0, uses=1)

    def draw(self, screen, font_sm, font_md, font_lg):
        x, y, w, h = self.x, self.y, self.w, self.h
        pygame.draw.rect(screen, (30,30,35), (x, y, w, h), border_radius=8)
        t = self.cfg["t"]; st = self.cfg["st"]
        # Title
        tl = "ЧИП ПРОШИТ!" if time.time()-self.flash_time < 1.5 else "ПРОГРАММАТОР ЧИПОВ"
        tc = (0,255,100) if "ПРОШИТ" in tl else (200,200,100)
        ts = font_md.render(tl, True, tc)
        screen.blit(ts, (x+w//2-ts.get_width()//2, y+8))
        # Category
        self.b_tp.draw(screen, font_sm); self.b_tn.draw(screen, font_sm)
        cs = font_sm.render(f"КАТ: {self.TYPES[t]}", True, (255,255,255))
        screen.blit(cs, (x+50, y+45))
        # Subtype
        subs = self.SUBTYPES[t]
        self.b_stp.draw(screen, font_sm); self.b_stn.draw(screen, font_sm)
        ss = font_sm.render(subs[st], True, (200,255,100))
        screen.blit(ss, (x+50, y+75))
        # Fields
        base_y = y + 100
        if t in [2, 3]:  # Арты/Броня
            screen.blit(font_sm.render("РЕГЕН:", True, (255,255,255)), (x+10, base_y))
            self.boxes[0].rect.topleft = (x+w-100, base_y); self.boxes[0].draw(screen, font_sm)
            screen.blit(font_sm.render("ИНТЕРВАЛ:", True, (255,255,255)), (x+10, base_y+28))
            self.boxes[17].rect.topleft = (x+w-100, base_y+28); self.boxes[17].draw(screen, font_sm)
            screen.blit(font_sm.render("ЗАЩИТА%", True, (100,255,100)), (x+w-100, base_y+55))
            if t == 2: screen.blit(font_sm.render("УРОН", True, (255,100,100)), (x+w-50, base_y+55))
            for i, (name, ri, di) in enumerate(self.DMG8):
                ry = base_y + 70 + i*26
                screen.blit(font_sm.render(name+":", True, (255,255,255)), (x+10, ry+3))
                self.boxes[ri].rect.topleft = (x+w-100, ry); self.boxes[ri].draw(screen, font_sm)
                if t == 2:
                    self.boxes[di].rect.topleft = (x+w-50, ry); self.boxes[di].draw(screen, font_sm)
            self.b_uses.rect.topleft = (x+w-100, base_y+280)
            screen.blit(font_sm.render("ПРОЧНОСТЬ:", True, (255,255,255)), (x+10, base_y+283))
            self.b_uses.draw(screen, font_sm)
        else:  # Админка/Расходники
            screen.blit(font_sm.render("ЗНАЧЕНИЕ:", True, (255,255,255)), (x+10, base_y))
            self.boxes[0].rect.topleft = (x+w-100, base_y); self.boxes[0].draw(screen, font_sm)
            self.b_uses.rect.topleft = (x+w-100, base_y+30)
            screen.blit(font_sm.render("КОЛИЧЕСТВО:", True, (255,255,255)), (x+10, base_y+33))
            self.b_uses.draw(screen, font_sm)
        # Flash button
        self.b_flash.draw(screen, font_sm, (50,150,50) if time.time()-self.flash_time > 0.5 else (200,200,200))

# =====================================================
# ПРОГРАММАТОР АНОМАЛИЙ (ПОЛНЫЙ)
# =====================================================
class AnomalyProgrammer:
    CATS = ["АНОМАЛИЯ", "УБЕЖИЩЕ"]
    SUBS = ["ВЗРЫВ","КРОВОТЕЧЕН.","ЖАРКА","ЭЛЕКТРА","КИСЕЛЬ","РАДИО","ПСИ-ПОЛЕ","КАРУСЕЛЬ","РАДИАЦИЯ"]
    RECHARGE = ["РЕАКТИВНЫЙ", "АВТОНОМНЫЙ"]
    TARGETS = ["ОДИН ИГРОК", "МИНА", "ЛУЖА"]

    def __init__(self, x, y, w, h):
        self.x, self.y, self.w, self.h = x, y, w, h
        self.cfg = {"cat": 0, "sub": 0, "rm": 0, "tg": 0}
        self.box_dmg = TextBox(0, 0, 45, 24, "10", 4)
        self.box_max = TextBox(0, 0, 45, 24, "20", 4)
        self.box_step = TextBox(0, 0, 45, 24, "1", 4)
        self.box_radius = TextBox(0, 0, 45, 24, "5", 4)
        self.box_sz_hp = TextBox(0, 0, 45, 24, "2", 4)
        self.box_sz_rad = TextBox(0, 0, 45, 24, "1", 4)
        self.box_sz_psy = TextBox(0, 0, 45, 24, "0", 4)
        self.box_sz_int = TextBox(0, 0, 45, 24, "5", 4)
        self.anom_boxes = [self.box_dmg, self.box_max, self.box_step, self.box_radius]
        self.sz_boxes = [self.box_sz_hp, self.box_sz_rad, self.box_sz_psy, self.box_sz_int]
        self.b_cp = NavButton(x+10, y+40, 30, 24, "<", "cp")
        self.b_cn = NavButton(x+w-40, y+40, 30, 24, ">", "cn")
        self.b_sp = NavButton(x+10, y+70, 30, 24, "<", "sp")
        self.b_sn = NavButton(x+w-40, y+70, 30, 24, ">", "sn")
        self.b_rp = NavButton(x+10, y+220, 30, 24, "<", "rp")
        self.b_rn = NavButton(x+w-40, y+220, 30, 24, ">", "rn")
        self.b_tp = NavButton(x+10, y+250, 30, 24, "<", "tp")
        self.b_tn = NavButton(x+w-40, y+250, 30, 24, ">", "tn")
        self.b_flash = NavButton(x+w//2-60, y+h-40, 120, 30, "ПРОШИТЬ", "flash")
        self.flash_time = 0

    def handle_click(self, mx, my):
        global active_textbox, world_anomalies
        if not pygame.Rect(self.x, self.y, self.w, self.h).collidepoint(mx, my): return
        sz = self.cfg["cat"] == 1
        boxes = self.sz_boxes if sz else self.anom_boxes
        for bx in boxes:
            if bx.rect.collidepoint(mx, my): active_textbox = bx; bx.active = True
            else: bx.active = False
        if self.b_cp.rect.collidepoint(mx, my): self.cfg["cat"] = 1 - self.cfg["cat"]; self.cfg["sub"] = 0
        if self.b_cn.rect.collidepoint(mx, my): self.cfg["cat"] = 1 - self.cfg["cat"]; self.cfg["sub"] = 0
        if not sz:
            if self.b_sp.rect.collidepoint(mx, my): self.cfg["sub"] = (self.cfg["sub"]-1) % 9
            if self.b_sn.rect.collidepoint(mx, my): self.cfg["sub"] = (self.cfg["sub"]+1) % 9
            if self.b_rp.rect.collidepoint(mx, my) or self.b_rn.rect.collidepoint(mx, my):
                self.cfg["rm"] = 1 - self.cfg["rm"]
            if self.b_tp.rect.collidepoint(mx, my): self.cfg["tg"] = (self.cfg["tg"]-1) % 3
            if self.b_tn.rect.collidepoint(mx, my): self.cfg["tg"] = (self.cfg["tg"]+1) % 3
        if self.b_flash.rect.collidepoint(mx, my):
            self.flash_time = time.time()
            gi = lambda b: int(b.text) if b.text not in ("","-") else 0
            if sz:
                interval = gi(self.box_sz_int)
                if interval <= 0: interval = 1  # минимум 1 сек
                world_anomalies.append({"cat":1,"sub":0,"dmg":0,"hp":gi(self.box_sz_hp),"rad":gi(self.box_sz_rad),"interval":interval})
            else:
                world_anomalies.append({"cat":0,"sub":self.cfg["sub"],"dmg":gi(self.box_dmg),
                                        "tg":self.cfg["tg"],"rm":self.cfg["rm"],
                                        "last_target":0})

    def draw(self, screen, font_sm, font_md):
        x, y, w, h = self.x, self.y, self.w, self.h
        sz = self.cfg["cat"] == 1
        bg = (25,40,30) if sz else (35,35,40)
        pygame.draw.rect(screen, bg, (x, y, w, h), border_radius=8)
        tl = "ПРОШИТО!" if time.time()-self.flash_time < 1.5 else "ПРОГРАММАТОР АНОМ."
        tc = (0,255,100) if "ПРОШИТО" in tl else (200,200,100)
        screen.blit(font_md.render(tl, True, tc), (x+w//2-60, y+8))
        self.b_cp.draw(screen, font_sm); self.b_cn.draw(screen, font_sm)
        screen.blit(font_sm.render(f"ТИП: {self.CATS[self.cfg['cat']]}", True, (255,200,100) if not sz else (100,255,150)), (x+50, y+45))
        if not sz:
            self.b_sp.draw(screen, font_sm); self.b_sn.draw(screen, font_sm)
            screen.blit(font_sm.render(self.SUBS[self.cfg["sub"]], True, (255,255,255)), (x+50, y+75))
            labels = [("СТАРТ УРОН:", self.box_dmg), ("МАКС УРОН:", self.box_max),
                      ("РОСТ/МИН:", self.box_step), ("РАДИУС:", self.box_radius)]
            for i, (lbl, bx) in enumerate(labels):
                ry = y + 100 + i*30
                screen.blit(font_sm.render(lbl, True, (255,255,255)), (x+10, ry+3))
                bx.rect.topleft = (x+w-70, ry); bx.draw(screen, font_sm)
            self.b_rp.draw(screen, font_sm); self.b_rn.draw(screen, font_sm)
            screen.blit(font_sm.render(f"РЕЖИМ: {self.RECHARGE[self.cfg['rm']]}", True, (200,200,200)), (x+50, y+225))
            self.b_tp.draw(screen, font_sm); self.b_tn.draw(screen, font_sm)
            screen.blit(font_sm.render(f"ЦЕЛИ: {self.TARGETS[self.cfg['tg']]}", True, (200,200,200)), (x+50, y+255))
        else:
            labels = [("РЕГЕН HP:", self.box_sz_hp), ("ОЧИСТКА RAD:", self.box_sz_rad),
                      ("ПСИ-ЗАЩИТА:", self.box_sz_psy), ("ИНТЕРВАЛ:", self.box_sz_int)]
            for i, (lbl, bx) in enumerate(labels):
                ry = y + 100 + i*30
                screen.blit(font_sm.render(lbl, True, (100,255,100)), (x+10, ry+3))
                bx.rect.topleft = (x+w-70, ry); bx.draw(screen, font_sm)
        self.b_flash.draw(screen, font_sm, (50,150,50) if time.time()-self.flash_time > 0.5 else (200,200,200))


# =====================================================
# MAIN LOOP
# =====================================================
def main():
    global selected_chip_idx, active_textbox, world_chips, world_anomalies
    screen = pygame.display.set_mode((WIN_W, WIN_H))
    pygame.display.set_caption("STALKER — ПОЛНЫЙ ЭМУЛЯТОР")
    clock = pygame.time.Clock()
    font_sm = pygame.font.SysFont("Consolas", 11)
    font_md = pygame.font.SysFont("Consolas", 14)
    font_lg = pygame.font.SysFont("Consolas", 16)

    # Разметка: 2 ПДА слева, программаторы справа
    pda1 = PDAPanel(5, 5, 5001, "pda1_state.json")
    pda2 = PDAPanel(5, 355, 5002, "pda2_state.json")
    chip_prog = ChipProgrammer(370, 5, 310, 420)
    anom_prog = AnomalyProgrammer(690, 5, 260, 420)

    # Панели созданных объектов
    chips_panel = pygame.Rect(370, 430, 310, 280)
    anoms_panel = pygame.Rect(690, 430, 260, 280)
    chip_scroll = 0; anom_scroll = 0

    running = True
    while running:
        mx, my = pygame.mouse.get_pos()
        pda1.tick(); pda2.tick()

        # Периодическая отправка пакетов от активных ЗЗ (каждые 2 сек)
        for an in world_anomalies:
            if an["cat"] == 1 and an.get("active", False):
                interval = an.get("interval", 2)
                if time.time() - an.get("last_send", 0) >= interval:
                    an["last_send"] = time.time()
                    sk = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                    pkt = create_safezone_packet(an.get("hp",0), an.get("rad",0)).pack()
                    sk.sendto(pkt, ('127.0.0.1', 5001))
                    sk.sendto(pkt, ('127.0.0.1', 5002))
                    sk.close()
        for e in pygame.event.get():
            if e.type == pygame.QUIT: running = False
            if e.type == pygame.MOUSEBUTTONDOWN and e.button == 1:
                pda1.handle_click(mx, my); pda2.handle_click(mx, my)
                chip_prog.handle_click(mx, my); anom_prog.handle_click(mx, my)
                # Выбор чипа из списка
                for i in range(min(len(world_chips), 15)):
                    r = pygame.Rect(chips_panel.x+5, chips_panel.y+25+i*25, chips_panel.w-10, 22)
                    if r.collidepoint(mx, my): selected_chip_idx = i
                # Активировать аномалию / переключить ЗЗ
                for i in range(min(len(world_anomalies), 15)):
                    r = pygame.Rect(anoms_panel.x+5, anoms_panel.y+25+i*25, anoms_panel.w-10, 22)
                    if r.collidepoint(mx, my):
                        an = world_anomalies[i]
                        if an["cat"] == 0:  # Аномалия — разовый удар
                            sk = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                            pkt = create_damage_packet(an["dmg"], an["sub"]).pack()
                            tg = an.get("tg", 0)
                            if tg == 0:  # ОДИН ИГРОК — чередуем PDA1/PDA2
                                target_port = 5001 if an.get("last_target",0) == 0 else 5002
                                sk.sendto(pkt, ('127.0.0.1', target_port))
                                an["last_target"] = 1 - an.get("last_target",0)
                                tgt_txt = f"PDA:{target_port}"
                            else:  # МИНА/ЛУЖА — всем
                                sk.sendto(pkt, ('127.0.0.1', 5001))
                                sk.sendto(pkt, ('127.0.0.1', 5002))
                                tgt_txt = "PDA:5001+5002"
                            sk.close()
                            an["last_fire"] = time.time()
                            ts = time.strftime("%H:%M:%S")
                            SUBS_N = ["ВЗРЫВ","КРОВЬ","ТЕРМО","ЭЛЕКТРА","КИСЕЛЬ","РАДИАЦ","ПСИ","КАРУС"]
                            sub_name = SUBS_N[an['sub']] if an['sub'] < len(SUBS_N) else "???"
                            event_log.append({"time":ts,"src":"АНОМ","text":f"УДАР {sub_name} DMG={an['dmg']} → {tgt_txt}","color":T_HP_LO})
                            if len(event_log) > MAX_LOG: event_log.pop(0)
                        else:  # ЗЗ — переключение вкл/выкл
                            an["active"] = not an.get("active", False)
                            an["last_send"] = 0  # сразу отправить при включении
                            st = "ВКЛ" if an["active"] else "ВЫКЛ"
                            ts = time.strftime("%H:%M:%S")
                            event_log.append({"time":ts,"src":"ЗЗ","text":f"БЕЗОП.ЗОНА: {st}","color":T_HP_HI})
                            if len(event_log) > MAX_LOG: event_log.pop(0)
                        break
            # Правый клик — удалить аномалию/ЗЗ из полигона
            if e.type == pygame.MOUSEBUTTONDOWN and e.button == 3:
                for i in range(min(len(world_anomalies), 15)):
                    r = pygame.Rect(anoms_panel.x+5, anoms_panel.y+25+i*25, anoms_panel.w-10, 22)
                    if r.collidepoint(mx, my):
                        world_anomalies.pop(i); break
                for i in range(min(len(world_chips), 15)):
                    r = pygame.Rect(chips_panel.x+5, chips_panel.y+25+i*25, chips_panel.w-10, 22)
                    if r.collidepoint(mx, my):
                        if i == selected_chip_idx: selected_chip_idx = -1
                        world_chips.pop(i); break
            if e.type == pygame.KEYDOWN:
                if e.key == pygame.K_F10:
                    pda1.set_event(pda1.player.register_player("ТЕСТ"))
                    pda2.set_event(pda2.player.register_player("ТЕСТ2"))
                elif active_textbox:
                    active_textbox.handle_key(e)

        # ======= ОТРИСОВКА =======
        screen.fill(C_BG)
        pda1.draw(screen, font_sm, font_md)
        pda2.draw(screen, font_sm, font_md)
        chip_prog.draw(screen, font_sm, font_md, font_lg)
        anom_prog.draw(screen, font_sm, font_md)

        # Панель чипов
        pygame.draw.rect(screen, (25,30,25), chips_panel, border_radius=6)
        screen.blit(font_md.render("СОЗДАННЫЕ ЧИПЫ", True, (0,200,200)), (chips_panel.x+10, chips_panel.y+5))
        for i in range(min(len(world_chips), 15)):
            c = world_chips[i]
            r = pygame.Rect(chips_panel.x+5, chips_panel.y+25+i*25, chips_panel.w-10, 22)
            clr = (50,255,50) if i == selected_chip_idx else (60,70,60)
            pygame.draw.rect(screen, clr, r, border_radius=3)
            n = c.get_name()
            info = f"{n} val={c.value}"
            if c.modifiers: info += f" mod={len(c.modifiers)}"
            screen.blit(font_sm.render(info, True, (255,255,255)), (r.x+5, r.y+4))

        # Панель аномалий / ЗЗ
        pygame.draw.rect(screen, (30,25,25), anoms_panel, border_radius=6)
        screen.blit(font_md.render("ПОЛИГОН (АНОМ/ЗЗ)", True, (200,100,100)), (anoms_panel.x+10, anoms_panel.y+5))
        SUBS_SHORT = ["ВЗРЫВ","КРОВЬ","ЖАРК","ЭЛЕКТ","ХИМИЯ","РАДИАЦ","ПСИ","ГРАВИ"]
        TG_SHORT = ["×1","AoE","AoE"]
        for i in range(min(len(world_anomalies), 15)):
            an = world_anomalies[i]
            r = pygame.Rect(anoms_panel.x+5, anoms_panel.y+25+i*25, anoms_panel.w-10, 22)
            # Подсветка
            if an["cat"] == 0:
                fired = time.time() - an.get("last_fire", 0) < 0.5
                bg_c = (160,60,60) if fired else (80,50,50)
            else:
                is_on = an.get("active", False)
                bg_c = (40,180,60) if is_on else (40,80,50)
            pygame.draw.rect(screen, bg_c, r, border_radius=3)
            if an["cat"] == 0:
                sub_name = SUBS_SHORT[an['sub']] if an['sub'] < len(SUBS_SHORT) else "???"
                tg_name = TG_SHORT[an.get("tg",0)]
                txt = f"{sub_name} DMG={an['dmg']} [{tg_name}]"
                label = "ЛКМ=УДАР"
            else:
                st = "●ВКЛ" if an.get("active",False) else "○ВЫКЛ"
                txt = f"ЗЗ HP+{an.get('hp',0)} RAD-{an.get('rad',0)}"
                label = st
            screen.blit(font_sm.render(txt, True, (255,255,255)), (r.x+5, r.y+4))
            screen.blit(font_sm.render(label, True, (255,200,200)), (r.x+r.w-65, r.y+4))

        # Подсказка
        if selected_chip_idx >= 0 and selected_chip_idx < len(world_chips):
            screen.blit(font_sm.render("▶ КЛИКНИТЕ ПО СЛОТУ ПДА, ЧТОБЫ ВСТАВИТЬ ЧИП", True, (0,255,100)), (370, WIN_H-12))

        # ======= ПАНЕЛЬ ЛОГОВ (низ окна) =======
        log_y = 715
        log_rect = pygame.Rect(5, log_y, WIN_W-10, WIN_H - log_y - 5)
        pygame.draw.rect(screen, (20, 22, 28), log_rect, border_radius=5)
        pygame.draw.rect(screen, (50, 55, 60), log_rect, 1, border_radius=5)
        screen.blit(font_sm.render("СИСТЕМНЫЙ ЛОГ", True, (0,200,200)), (12, log_y+3))
        visible_logs = event_log[-(log_rect.h // 14 - 1):]
        for i, entry in enumerate(visible_logs):
            ly = log_y + 16 + i * 13
            if ly + 13 > log_rect.y + log_rect.h: break
            line = f"[{entry['time']}] {entry['src']}: {entry['text']}"
            screen.blit(font_sm.render(line, True, entry['color']), (12, ly))

        pygame.display.flip()
        clock.tick(FPS)

    pygame.quit()

if __name__ == "__main__":
    main()
