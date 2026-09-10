"""
Программатор внутри окна мастера (не отдельный pygame).
USB — CHIP_BOX / аномалия / убежище / ПДА на столе / терминал.
ПДА игрока в поле — через LoRa или EEPROM-чип, не этим модулем.
"""

import os
import sys
import tkinter as tk
from tkinter import ttk, messagebox

from theme import module_header

PROGRAMMER_DIR = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "programmat_pc")
)
if PROGRAMMER_DIR not in sys.path:
    sys.path.insert(0, PROGRAMMER_DIR)

from config_builder import build_config_for_device  # noqa: E402

CHIP_TYPES = ["РАСХОДНИКИ", "БРОНЯ", "АРТЕФАКТЫ", "АДМИНКА"]
CHIP_SUBTYPES = {
    0: ["МГНОВЕННОЕ ЛЕЧЕНИЕ", "АНТИРАДИН", "РЕГЕНЕРАТОР",
        "СТИМУЛЯТОР", "ВОССТАНОВЛЕНИЕ", "УЛУЧШЕНИЕ"],
    1: ["УНИВЕРСАЛЬНЫЙ ЧИП"],
    2: ["УНИВЕРСАЛЬНЫЙ ЧИП"],
    3: ["ВОСКРЕШЕНИЕ", "УПРАВЛЕНИЕ ДЕНЬГАМИ", "УРОВЕНЬ/ОПЫТ",
        "ИММУНИТЕТ", "СБРОС", "НЕЙТРАЛИЗАЦИЯ", "ДОПУСК В ИГРУ",
        "РЕГИСТРАЦИЯ"],
}
ANOM_TYPES = [
    "ВЗРЫВ", "КРОВЬ", "ТЕРМО", "ЭЛЕКТРО", "ХИМИЯ", "ПСИ", "ГРАВИТ.",
]
# биты 0-4, 6, 7 (бит 5 зарезервирован под RAD в протоколе HP-маски)
ANOM_BITS = [0, 1, 2, 3, 4, 6, 7]
FUNC_LABELS = [
    "HP", "RAD", "Деньги", "Броня", "Артефакты", "Аномалии", "Уровни", "Расходники",
]
PROT_LABELS = ["Взрыв", "Кровь", "Термо", "Электро", "Химия", "Пси", "Гравит.", "RAD"]
TERMINAL_ROLES = ("STORE", "ATM", "QUEST", "ADMIT", "BANK")
RECHARGE_MODES = ["РЕАКТИВНЫЙ", "АВТОНОМНЫЙ"]
TARGET_MODES = ["ОДИН ИГРОК", "ПОДРЫВ", "ОБЛАКО"]


class ProgrammerFrame(ttk.Frame):
    def __init__(self, master, serial=None, db=None, event_id=None, on_back=None):
        super().__init__(master)
        self.serial = serial
        self.db = db
        self.event_id = event_id
        self.on_back = on_back
        self.anom_bits = [tk.BooleanVar(value=(i == 0)) for i in range(7)]
        self.sz_prot = [tk.StringVar(value="0") for _ in range(8)]
        self.pda_prot = [tk.StringVar(value="0") for _ in range(8)]
        self.pda_func = [tk.BooleanVar(value=True) for _ in range(8)]
        self._build()

    def _build(self):
        module_header(self, "Программатор", self.on_back)
        ttk.Label(
            self,
            text="Arduino / .ino заливается один раз на базе. "
                 "Дальше подключите устройство USB и нажмите «Записать в устройство» — "
                 "роль, лимиты, задания, аномалия, убежище, чип.",
            style="Dim.TLabel",
            wraplength=820,
        ).pack(anchor="w", padx=12, pady=(0, 6))

        nb = ttk.Notebook(self)
        nb.pack(fill="both", expand=True, padx=12, pady=(0, 8))
        self.nb = nb

        self.tab_chip = ttk.Frame(nb)
        self.tab_anom = ttk.Frame(nb)
        self.tab_sz = ttk.Frame(nb)
        self.tab_pda = ttk.Frame(nb)
        self.tab_term = ttk.Frame(nb)
        nb.add(self.tab_chip, text="Чип")
        nb.add(self.tab_anom, text="Аномалия")
        nb.add(self.tab_sz, text="Убежище")
        nb.add(self.tab_pda, text="ПДА (стол)")
        nb.add(self.tab_term, text="Терминал")

        self._build_chip()
        self._build_anom()
        self._build_sz()
        self._build_pda()
        self._build_term()

        bar = ttk.Frame(self)
        bar.pack(fill="x", padx=12, pady=(0, 10))
        ttk.Button(bar, text="Записать в устройство", style="Accent.TButton",
                   command=self._flash).pack(side="left")
        ttk.Button(bar, text="Считать с устройства",
                   command=self._read).pack(side="left", padx=8)
        self.lbl_status = ttk.Label(bar, text="", style="Dim.TLabel")
        self.lbl_status.pack(side="left", padx=12)

    def _row(self, parent, label, var, width=12):
        r = ttk.Frame(parent)
        r.pack(fill="x", padx=8, pady=2)
        ttk.Label(r, text=label, width=22).pack(side="left")
        ttk.Entry(r, textvariable=var, width=width).pack(side="left")
        return r

    def _build_chip(self):
        f = self.tab_chip
        self.var_chip_uses = tk.StringVar(value="1")
        self.var_reg_name = tk.StringVar()
        self.var_pct = tk.BooleanVar(value=False)
        self.field_vars = {}

        top = ttk.Frame(f)
        top.pack(fill="x", padx=8, pady=8)
        ttk.Label(top, text="Тип").pack(side="left")
        self.cmb_chip_type = ttk.Combobox(
            top, state="readonly", width=18, values=CHIP_TYPES)
        self.cmb_chip_type.current(0)
        self.cmb_chip_type.pack(side="left", padx=6)
        self.cmb_chip_type.bind("<<ComboboxSelected>>", self._on_chip_type)
        ttk.Label(top, text="Что за чип").pack(side="left", padx=(12, 0))
        self.cmb_chip_sub = ttk.Combobox(top, state="readonly", width=28)
        self.cmb_chip_sub.pack(side="left", padx=6)
        self.cmb_chip_sub.bind("<<ComboboxSelected>>", lambda _e: self._refresh_chip_fields())
        ttk.Label(top, text="Сколько раз можно использовать").pack(side="left", padx=(12, 0))
        ttk.Entry(top, textvariable=self.var_chip_uses, width=6).pack(side="left", padx=4)

        self.chip_fields = ttk.LabelFrame(f, text="Параметры (человеческим языком)")
        self.chip_fields.pack(fill="both", expand=True, padx=8, pady=8)
        self._refresh_subs()
        self._refresh_chip_fields()

    def _refresh_subs(self):
        t = self.cmb_chip_type.current()
        if t < 0:
            t = 0
        subs = CHIP_SUBTYPES[t]
        self.cmb_chip_sub["values"] = subs
        self.cmb_chip_sub.current(0)

    def _on_chip_type(self, _evt=None):
        self._refresh_subs()
        self._refresh_chip_fields()

    def _add_labeled(self, parent, key, label, unit="", default="0"):
        var = self.field_vars.get(key)
        if var is None:
            var = tk.StringVar(value=default)
            self.field_vars[key] = var
        r = ttk.Frame(parent)
        r.pack(fill="x", padx=8, pady=3)
        ttk.Label(r, text=label, width=28).pack(side="left")
        ttk.Entry(r, textvariable=var, width=10).pack(side="left")
        if unit:
            ttk.Label(r, text=unit, style="Dim.TLabel").pack(side="left", padx=6)

    def _prot_grid(self, parent, prefix="prot"):
        box = ttk.LabelFrame(parent, text="Защита от урона, %")
        box.pack(fill="x", padx=8, pady=6)
        for i, name in enumerate(PROT_LABELS):
            key = f"{prefix}{i}"
            var = self.field_vars.get(key)
            if var is None:
                var = tk.StringVar(value="0")
                self.field_vars[key] = var
            cell = ttk.Frame(box)
            cell.grid(row=i // 4, column=i % 4, padx=4, pady=3, sticky="w")
            ttk.Label(cell, text=name, width=10).pack(side="left")
            ttk.Entry(cell, textvariable=var, width=5).pack(side="left")

    def _refresh_chip_fields(self):
        for w in self.chip_fields.winfo_children():
            w.destroy()
        t = max(0, self.cmb_chip_type.current())
        st = max(0, self.cmb_chip_sub.current())
        f = self.chip_fields
        ttk.Checkbutton(
            f, text="Значения в процентах (где есть выбор HP / %)",
            variable=self.var_pct,
        ).pack(anchor="w", padx=8, pady=(4, 2))

        if t == 0:
            if st == 0:
                self._add_labeled(f, "heal", "Вылечить", "HP или % от запаса")
            elif st == 1:
                self._add_labeled(f, "rad", "Снять радиацию", "RAD или %")
            elif st == 2:
                self._add_labeled(f, "regen_hp", "Восстановление жизней", "в секунду")
                self._add_labeled(f, "regen_hp_t", "Сколько секунд действует (HP)", "сек")
                self._add_labeled(f, "regen_rad", "Очистка радиации", "в секунду")
                self._add_labeled(f, "regen_rad_t", "Сколько секунд действует (RAD)", "сек")
            elif st == 3:
                self._prot_grid(f)
                self._add_labeled(f, "stim_t", "Длительность стимулятора", "сек")
            elif st == 4:
                self._add_labeled(f, "restore", "Починить предмет", "единицы или %")
            elif st == 5:
                self._add_labeled(f, "upgrade_hp", "Добавить прочности предмету", "ед., макс 250")
                self._add_labeled(f, "upgrade_pct", "Улучшить параметры", "%")
        elif t == 1:
            self._add_labeled(f, "arm_regen", "Реген жизней в броне", "HP в минуту")
            self._add_labeled(f, "arm_int", "Как часто реген", "сек")
            self._add_labeled(f, "arm_bonus", "Дополнительные жизни", "HP")
            self._prot_grid(f)
        elif t == 2:
            self._add_labeled(f, "art_regen", "Реген жизней артефакта", "HP в минуту")
            ttk.Label(f, text="Защита и урон по типам — ниже.",
                      style="Dim.TLabel").pack(anchor="w", padx=8)
            self._prot_grid(f)
        elif t == 3:
            if st == 0:
                ttk.Label(f, text="Воскрешение: параметров нет — чип просто поднимает игрока.",
                          style="Dim.TLabel").pack(anchor="w", padx=8, pady=8)
            elif st == 1:
                self._add_labeled(f, "money", "Сумма (плюс или минус)", "руб")
            elif st == 2:
                self._add_labeled(f, "lvl", "Изменить уровень", "")
                self._add_labeled(f, "xp", "Изменить опыт", "XP")
            elif st == 3:
                self._add_labeled(f, "imm", "Длительность иммунитета", "мин")
            elif st in (4, 5):
                ttk.Label(f, text="Команда без чисел: сброс или нейтрализация.",
                          style="Dim.TLabel").pack(anchor="w", padx=8, pady=8)
            elif st == 6:
                self._add_labeled(f, "adm_h", "Допуск через сколько часов", "ч")
                self._add_labeled(f, "adm_m", "и минут", "мин")
            elif st == 7:
                self._add_labeled(f, "reg_id", "ID игрока на этом событии", "", default="1")
                r = ttk.Frame(f)
                r.pack(fill="x", padx=8, pady=3)
                ttk.Label(r, text="Имя на чип (не в эфир LoRa)", width=28).pack(side="left")
                ttk.Entry(r, textvariable=self.var_reg_name, width=24).pack(side="left")
                ttk.Label(
                    f,
                    text="Регистрация идёт через мост: устройство мастера + общий чип + шнур к ПДА.",
                    style="Dim.TLabel", wraplength=720,
                ).pack(anchor="w", padx=8, pady=6)

    def _fv(self, key, default=0):
        var = self.field_vars.get(key)
        if var is None:
            return default
        return self._i(var, default)

    def _chip_dict(self):
        t = max(0, self.cmb_chip_type.current())
        st = max(0, self.cmb_chip_sub.current())
        p = [0] * 16
        pct = 1 if self.var_pct.get() else 0
        if t == 0:
            if st == 0:
                p[0], p[1] = self._fv("heal"), pct
            elif st == 1:
                p[0], p[1] = self._fv("rad"), pct
            elif st == 2:
                p[0] = self._fv("regen_hp")
                p[1] = pct
                p[2] = self._fv("regen_hp_t")
                p[3] = self._fv("regen_rad")
                p[4] = pct
                p[5] = self._fv("regen_rad_t")
            elif st == 3:
                for i in range(8):
                    p[i] = self._fv(f"prot{i}")
                p[8] = self._fv("stim_t")
            elif st == 4:
                p[0], p[1] = self._fv("restore"), pct
            elif st == 5:
                p[0] = self._fv("upgrade_hp")
                p[1] = self._fv("upgrade_pct")
        elif t == 1:
            for i in range(8):
                p[i] = self._fv(f"prot{i}")
            interval = max(1, self._fv("arm_int", 30))
            regen = self._fv("arm_regen")
            p[8] = round(regen * interval / 60) if interval else regen
            p[9] = interval
            p[10] = self._fv("arm_bonus")
        elif t == 2:
            interval = 60
            regen = self._fv("art_regen")
            p[0] = round(regen * interval / 60) if interval else regen
            p[1] = interval
            for i in range(7):
                p[2 + i] = self._fv(f"prot{i}")
            p[9] = self._fv("prot7")
        elif t == 3:
            if st == 1:
                p[0] = self._fv("money")
            elif st == 2:
                p[0], p[1] = self._fv("lvl"), self._fv("xp")
            elif st == 3:
                p[0] = self._fv("imm")
            elif st == 6:
                p[0], p[1] = self._fv("adm_h"), self._fv("adm_m")
            elif st == 7:
                p[0] = self._fv("reg_id", 1)
        return {
            "chip_type": t,
            "chip_sub": st,
            "uses": max(0, min(255, self._i(self.var_chip_uses, 1))),
            "params": p,
            "reg_name": self.var_reg_name.get().strip(),
        }

    def _build_anom(self):
        f = self.tab_anom
        self.var_anom_dmg = tk.StringVar(value="10")
        self.var_anom_dmax = tk.StringVar(value="20")
        self.var_anom_dstep = tk.StringVar(value="1")
        self.var_anom_freq = tk.StringVar(value="60")
        self.var_anom_erupt = tk.StringVar(value="30")
        self.var_anom_hits = tk.StringVar(value="3")
        self.var_anom_radius = tk.StringVar(value="5")
        self.var_anom_rad_on = tk.BooleanVar(value=False)
        self.var_anom_rad_dmg = tk.StringVar(value="0")
        self.var_anom_rad_freq = tk.StringVar(value="10")

        bits = ttk.LabelFrame(f, text="Типы урона HP")
        bits.pack(fill="x", padx=8, pady=8)
        for i, name in enumerate(ANOM_TYPES):
            ttk.Checkbutton(bits, text=name, variable=self.anom_bits[i]).pack(
                side="left", padx=4)

        self._row(f, "Урон", self.var_anom_dmg)
        self._row(f, "Урон макс.", self.var_anom_dmax)
        self._row(f, "Шаг урона", self.var_anom_dstep)
        self._row(f, "Перезарядка HP, сек", self.var_anom_freq)
        self._row(f, "Извержение, сек", self.var_anom_erupt)
        self._row(f, "Попадания", self.var_anom_hits)
        self._row(f, "Радиус", self.var_anom_radius)

        rec = ttk.Frame(f)
        rec.pack(fill="x", padx=8, pady=4)
        ttk.Label(rec, text="Перезарядка / цель", width=22).pack(side="left")
        self.cmb_rech = ttk.Combobox(rec, state="readonly", width=16,
                                     values=RECHARGE_MODES)
        self.cmb_rech.current(0)
        self.cmb_tgt = ttk.Combobox(rec, state="readonly", width=16,
                                    values=TARGET_MODES)
        self.cmb_tgt.current(0)
        self.cmb_rech.pack(side="left", padx=4)
        self.cmb_tgt.pack(side="left", padx=4)

        ttk.Checkbutton(f, text="Радиация (отдельный таймер)",
                        variable=self.var_anom_rad_on).pack(anchor="w", padx=8)
        self._row(f, "RAD за тик", self.var_anom_rad_dmg)
        self._row(f, "RAD интервал, сек", self.var_anom_rad_freq)

    def _build_sz(self):
        f = self.tab_sz
        self.var_sz_regen = tk.StringVar(value="5")
        self.var_sz_rad = tk.StringVar(value="2")
        self.var_sz_hp_freq = tk.StringVar(value="30")
        self.var_sz_rad_freq = tk.StringVar(value="60")
        self.var_sz_radius = tk.StringVar(value="10")
        self.var_sz_emission = tk.BooleanVar(value=False)
        self._row(f, "Реген HP / мин", self.var_sz_regen)
        self._row(f, "Интервал HP, сек", self.var_sz_hp_freq)
        self._row(f, "Очистка RAD", self.var_sz_rad)
        self._row(f, "Интервал RAD, сек", self.var_sz_rad_freq)
        self._row(f, "Радиус", self.var_sz_radius)
        ttk.Checkbutton(f, text="Защита от выброса",
                        variable=self.var_sz_emission).pack(anchor="w", padx=8, pady=4)
        prot = ttk.LabelFrame(f, text="Защиты %")
        prot.pack(fill="x", padx=8, pady=8)
        for i, name in enumerate(PROT_LABELS):
            cell = ttk.Frame(prot)
            cell.grid(row=i // 4, column=i % 4, padx=4, pady=4, sticky="w")
            ttk.Label(cell, text=name, width=10).pack(side="left")
            ttk.Entry(cell, textvariable=self.sz_prot[i], width=5).pack(side="left")

    def _build_pda(self):
        f = self.tab_pda
        ttk.Label(
            f,
            text="Стендовая прошивка ПДА на столе (функции / пресет). "
                 "Имя игрока — чип регистрации, не этот экран.",
            style="Dim.TLabel", wraplength=760,
        ).pack(anchor="w", padx=8, pady=8)
        self.var_pda_mode = tk.IntVar(value=0)
        ttk.Radiobutton(f, text="Функции (биты)", variable=self.var_pda_mode,
                        value=0).pack(anchor="w", padx=8)
        box = ttk.Frame(f)
        box.pack(fill="x", padx=16, pady=4)
        for i, lab in enumerate(FUNC_LABELS):
            ttk.Checkbutton(box, text=lab, variable=self.pda_func[i]).grid(
                row=i // 4, column=i % 4, sticky="w", padx=6, pady=2)

        ttk.Radiobutton(f, text="Пресет данных", variable=self.var_pda_mode,
                        value=1).pack(anchor="w", padx=8, pady=(8, 2))
        self.var_pda_maxhp = tk.StringVar(value="1000")
        self.var_pda_starthp = tk.StringVar(value="1000")
        self.var_pda_maxrad = tk.StringVar(value="1000")
        self.var_pda_money = tk.StringVar(value="1000")
        self.var_pda_level = tk.StringVar(value="2")
        self.var_pda_xp = tk.StringVar(value="0")
        self._row(f, "Макс. HP", self.var_pda_maxhp)
        self._row(f, "Старт HP", self.var_pda_starthp)
        self._row(f, "Макс. RAD", self.var_pda_maxrad)
        self._row(f, "Деньги", self.var_pda_money)
        self._row(f, "Уровень", self.var_pda_level)
        self._row(f, "XP", self.var_pda_xp)
        prot = ttk.LabelFrame(f, text="Базовые защиты %")
        prot.pack(fill="x", padx=8, pady=8)
        for i, name in enumerate(PROT_LABELS):
            cell = ttk.Frame(prot)
            cell.grid(row=i // 4, column=i % 4, padx=4, pady=4, sticky="w")
            ttk.Label(cell, text=name, width=10).pack(side="left")
            ttk.Entry(cell, textvariable=self.pda_prot[i], width=5).pack(side="left")

    def _build_term(self):
        f = self.tab_term
        ttk.Label(
            f,
            text="Один корпус ESP. Роль и лимиты пишутся в память устройства (NVS), "
                 "прошивку заново заливать не нужно. 0 = без лимита.",
            style="Dim.TLabel", wraplength=760,
        ).pack(anchor="w", padx=8, pady=(8, 4))

        r = ttk.Frame(f)
        r.pack(fill="x", padx=8, pady=4)
        ttk.Label(r, text="Роль", width=22).pack(side="left")
        self.cmb_role = ttk.Combobox(r, state="readonly", width=16,
                                     values=TERMINAL_ROLES)
        self.cmb_role.current(0)
        self.cmb_role.pack(side="left")

        self.var_lim_buy = tk.StringVar(value="0")
        self.var_lim_wd = tk.StringVar(value="0")
        self.var_lim_dep = tk.StringVar(value="0")
        self._row(f, "Касса: макс. покупка, RUB", self.var_lim_buy)
        self._row(f, "Банкомат: макс. снятие, RUB", self.var_lim_wd)
        self._row(f, "Банкомат: макс. вклад, RUB", self.var_lim_dep)

        self.var_flash_quests = tk.BooleanVar(value=True)
        ttk.Checkbutton(
            f,
            text="Если роль QUEST — записать задания с доски этого события",
            variable=self.var_flash_quests,
        ).pack(anchor="w", padx=8, pady=(10, 4))
        ttk.Label(
            f,
            text="Задания редактируются в меню «Задания». Здесь только отправка на доску.",
            style="Dim.TLabel", wraplength=760,
        ).pack(anchor="w", padx=8, pady=(0, 8))

    def _i(self, var, default=0):
        try:
            return int(var.get())
        except (TypeError, ValueError, tk.TclError):
            return default

    def _anom_dict(self):
        mask = 0
        for i, bit in enumerate(ANOM_BITS):
            if self.anom_bits[i].get():
                mask |= 1 << bit
        if mask == 0:
            mask = 1
        return {
            "anom_dmg_mask": mask,
            "anom_dmg": self._i(self.var_anom_dmg, 10),
            "anom_dmg_max": self._i(self.var_anom_dmax, 20),
            "anom_dmg_step": self._i(self.var_anom_dstep, 1),
            "anom_freq": self._i(self.var_anom_freq, 60),
            "anom_erupt": self._i(self.var_anom_erupt, 30),
            "anom_hits": self._i(self.var_anom_hits, 3),
            "anom_radius": self._i(self.var_anom_radius, 5),
            "anom_rad_on": bool(self.var_anom_rad_on.get()),
            "anom_rad_dmg": self._i(self.var_anom_rad_dmg),
            "anom_rad_freq": self._i(self.var_anom_rad_freq, 10),
            "anom_recharge": max(0, self.cmb_rech.current()),
            "anom_target": max(0, self.cmb_tgt.current()),
        }

    def _sz_dict(self):
        return {
            "sz_regen": self._i(self.var_sz_regen, 5),
            "sz_rad": self._i(self.var_sz_rad, 2),
            "sz_hp_freq": self._i(self.var_sz_hp_freq, 30),
            "sz_rad_freq": self._i(self.var_sz_rad_freq, 60),
            "sz_radius": self._i(self.var_sz_radius, 10),
            "sz_emission": bool(self.var_sz_emission.get()),
            "sz_prot": [self._i(v) for v in self.sz_prot],
        }

    def _pda_dict(self):
        flags = 0
        for i, v in enumerate(self.pda_func):
            if v.get():
                flags |= 1 << i
        return {
            "pda_mode": self.var_pda_mode.get(),
            "pda_func_flags": flags,
            "pda_maxhp": self._i(self.var_pda_maxhp, 1000),
            "pda_starthp": self._i(self.var_pda_starthp, 1000),
            "pda_maxrad": self._i(self.var_pda_maxrad, 1000),
            "pda_money": self._i(self.var_pda_money, 1000),
            "pda_level": self._i(self.var_pda_level, 2),
            "pda_xp": self._i(self.var_pda_xp),
            "pda_base_prot": [self._i(v) for v in self.pda_prot],
        }

    def _flash(self):
        if not self.serial:
            messagebox.showwarning("Программатор", "Serial недоступен", parent=self)
            return
        tab = self.nb.index(self.nb.select())
        if tab == 4:
            role = self.cmb_role.get() or "STORE"
            ok, resp = self.serial.configure_terminal(
                role,
                limit_purchase=self._i(self.var_lim_buy, 0),
                limit_withdraw=self._i(self.var_lim_wd, 0),
                limit_deposit=self._i(self.var_lim_dep, 0),
            )
            if ok and role == "QUEST" and self.var_flash_quests.get():
                if not self.db or not self.event_id:
                    self._done(True, (resp or "OK") + "  (задания: нет события)")
                    return
                quests = self.db.list_quests(self.event_id)
                if not quests:
                    self._done(True, (resp or "OK") + "  (каталог заданий пуст)")
                    return
                qok, qmsg = self.serial.flash_quest_board(quests)
                if not qok:
                    self._done(False, "Роль/лимиты записаны, задания нет: " + str(qmsg))
                    return
                self._done(True, f"{resp}\nЗадания: {qmsg}")
                return
            self._done(ok, resp)
            return
        kind = ("CHIP", "ANOMALY", "SAFE_ZONE", "PDA")[tab]
        data = (self._chip_dict, self._anom_dict, self._sz_dict, self._pda_dict)[tab]()
        cfg = build_config_for_device(kind, data)
        ok, resp = self.serial.flash_config(cfg)
        self._done(ok, resp)

    def _read(self):
        if not self.serial:
            return
        snap, msg = self.serial.read_config()
        if snap is None:
            messagebox.showerror("Программатор", str(msg), parent=self)
            return
        if isinstance(snap, dict):
            role = str(snap.get("role") or "").upper()
            if role in TERMINAL_ROLES:
                self.cmb_role.set(role)
                self.nb.select(self.tab_term)
            if "limit_purchase" in snap:
                self.var_lim_buy.set(str(snap.get("limit_purchase") or 0))
            if "limit_withdraw" in snap:
                self.var_lim_wd.set(str(snap.get("limit_withdraw") or 0))
            if "limit_deposit" in snap:
                self.var_lim_dep.set(str(snap.get("limit_deposit") or 0))
        messagebox.showinfo("Считано", str(snap), parent=self)

    def _done(self, ok, resp):
        self.lbl_status.configure(text=("OK  " if ok else "Ошибка  ") + str(resp or ""))
        if ok:
            messagebox.showinfo("Программатор", str(resp), parent=self)
        else:
            messagebox.showwarning("Программатор", str(resp), parent=self)
