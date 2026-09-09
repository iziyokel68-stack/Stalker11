"""
STALKER App — модуль «Сбор статистики» (docs/PROGRESSION.txt §10.1, §10.3 Фаза 3)
====================================================================================
Съём снимка по игроку (уровень, XP, деньги, смерти, cheat_shield_count) в конце
игры/смены и сводный экспорт CSV.

Съём с реального ПДА (Serial) — по канону через слот/EEPROM после боевой
прошивки (ROADMAP §4.3). Сейчас снимок вводится мастером вручную — форма
готова принять те же поля, что появятся при автосъёме.
"""

import json
import tkinter as tk
from tkinter import ttk, messagebox, filedialog

from db.event_db import EventDB


class StatsFrame(ttk.Frame):
    def __init__(self, master, db: EventDB, event_id: str, on_back=None):
        super().__init__(master)
        self.db = db
        self.event_id = event_id
        self.on_back = on_back
        self.selected_player_id = None

        self._build_ui()
        self.refresh_players()

    # ------------------------------------------------------------------

    def _build_ui(self):
        top = ttk.Frame(self)
        top.pack(fill="x", padx=10, pady=8)
        ttk.Label(top, text="Сбор статистики", font=("Segoe UI", 14, "bold")).pack(side="left")
        if self.on_back:
            ttk.Button(top, text="← Меню", command=self.on_back).pack(side="right")

        body = ttk.Frame(self)
        body.pack(fill="both", expand=True, padx=10, pady=(0, 10))

        left = ttk.LabelFrame(body, text="Игрок")
        left.pack(side="left", fill="y")

        self.player_list = tk.Listbox(left, width=28, height=18, exportselection=False)
        self.player_list.pack(padx=6, pady=6, fill="y")
        self.player_list.bind("<<ListboxSelect>>", self._on_select_player)
        self._player_ids = []

        mid = ttk.LabelFrame(body, text="Снимок (конец игры / смены)")
        mid.pack(side="left", fill="both", expand=True, padx=(10, 0))

        self.var_level = tk.StringVar()
        self.var_xp = tk.StringVar()
        self.var_money = tk.StringVar()
        self.var_deaths = tk.StringVar()
        self.var_shield = tk.StringVar()
        self.var_rank = tk.StringVar()

        def row(label, var):
            r = ttk.Frame(mid)
            r.pack(fill="x", padx=8, pady=4)
            ttk.Label(r, text=label, width=20).pack(side="left")
            ttk.Entry(r, textvariable=var, width=20).pack(side="left")

        row("Уровень", self.var_level)
        row("XP", self.var_xp)
        row("Деньги (RUB)", self.var_money)
        row("Смерти", self.var_deaths)
        row("cheat_shield_count", self.var_shield)
        row("Ранг (титул)", self.var_rank)

        ttk.Button(mid, text="Записать снимок", command=self._record_stat).pack(
            fill="x", padx=8, pady=(10, 4))

        right = ttk.LabelFrame(body, text="История снимков события")
        right.pack(side="left", fill="both", expand=True, padx=(10, 0))

        columns = ("time", "name", "level", "xp", "money", "deaths", "shield")
        self.tree = ttk.Treeview(right, columns=columns, show="headings", height=18)
        headers = {"time": "Время", "name": "Игрок", "level": "Ур.",
                   "xp": "XP", "money": "RUB", "deaths": "Смерти", "shield": "Античит"}
        widths = {"time": 110, "name": 110, "level": 40, "xp": 60,
                  "money": 60, "deaths": 55, "shield": 65}
        for c in columns:
            self.tree.heading(c, text=headers[c])
            self.tree.column(c, width=widths[c], anchor="w")
        self.tree.pack(fill="both", expand=True, padx=6, pady=6)

        ttk.Button(right, text="Экспорт CSV (статистика)", command=self._export_csv).pack(
            fill="x", padx=6, pady=(0, 6))

    # ------------------------------------------------------------------

    def refresh_players(self):
        self.player_list.delete(0, "end")
        self._player_ids = []
        for p in self.db.list_players(self.event_id):
            label = p.name + (f" ({p.callsign})" if p.callsign else "")
            self.player_list.insert("end", label)
            self._player_ids.append(p.player_id)
        self._refresh_history()

    def _on_select_player(self, _evt=None):
        sel = self.player_list.curselection()
        if not sel:
            return
        self.selected_player_id = self._player_ids[sel[0]]
        last = self.db.latest_stat(self.selected_player_id)
        if last:
            self.var_level.set(str(last.level or ""))
            self.var_xp.set(str(last.xp or ""))
            self.var_money.set(str(last.money_rub or ""))
            self.var_deaths.set(str(last.deaths or ""))
            self.var_shield.set(str(last.cheat_shield_count or ""))
            self.var_rank.set(last.rank_title or "")
        else:
            for v in (self.var_level, self.var_xp, self.var_money,
                      self.var_deaths, self.var_shield, self.var_rank):
                v.set("")

    def _int_or_none(self, s):
        s = s.strip()
        if not s:
            return None
        try:
            return int(s)
        except ValueError:
            return None

    def _record_stat(self):
        if self.selected_player_id is None:
            messagebox.showinfo("Статистика", "Выберите игрока в списке")
            return
        self.db.record_stat(
            self.selected_player_id, self.event_id,
            level=self._int_or_none(self.var_level.get()),
            xp=self._int_or_none(self.var_xp.get()),
            money_rub=self._int_or_none(self.var_money.get()),
            deaths=self._int_or_none(self.var_deaths.get()),
            cheat_shield_count=self._int_or_none(self.var_shield.get()),
            rank_title=self.var_rank.get().strip() or None,
        )
        self._refresh_history()
        messagebox.showinfo("Статистика", "Снимок записан")

    def _refresh_history(self):
        for i in self.tree.get_children():
            self.tree.delete(i)
        players_by_id = {p.player_id: p for p in self.db.list_players(self.event_id)}
        for s in self.db.list_stats(self.event_id):
            p = players_by_id.get(s.player_id)
            name = p.name if p else "?"
            import time as _time
            t = _time.strftime("%H:%M:%S", _time.localtime(s.collected_at))
            self.tree.insert("", "end", values=(
                t, name, s.level or "", s.xp or "", s.money_rub or "",
                s.deaths or "", s.cheat_shield_count or "",
            ))

    def _export_csv(self):
        path = filedialog.asksaveasfilename(
            title="Экспорт статистики",
            defaultextension=".csv",
            filetypes=[("CSV", "*.csv")],
            initialfile="stats.csv",
        )
        if not path:
            return
        self.db.export_stats_csv(self.event_id, path)
        messagebox.showinfo("Экспорт", f"Сохранено: {path}")
