"""
STALKER App — модуль «Регистрация» (docs/PROGRESSION.txt §10.1, §10.3 Фаза 2)
================================================================================
Список игроков события, форма добавления/редактирования, экспорт в CSV.

Привязка PDA (bind_pda) заложена в EventDB уже сейчас — по канону будет
делаться через слот EEPROM/общий чип после основной прошивки (см. §9.6);
поле pda_uid можно проставить и вручную, пока нет проводного сценария.
"""

import tkinter as tk
from tkinter import ttk, messagebox, filedialog

from db.event_db import EventDB


class RegistrationFrame(ttk.Frame):
    def __init__(self, master, db: EventDB, event_id: str, on_back=None):
        super().__init__(master)
        self.db = db
        self.event_id = event_id
        self.on_back = on_back
        self.selected_player_id = None

        self._build_ui()
        self.refresh()

    # ------------------------------------------------------------------

    def _build_ui(self):
        top = ttk.Frame(self)
        top.pack(fill="x", padx=10, pady=8)

        ttk.Label(top, text="Регистрация игроков", font=("Segoe UI", 14, "bold")).pack(side="left")
        if self.on_back:
            ttk.Button(top, text="← Меню", command=self.on_back).pack(side="right")

        body = ttk.Frame(self)
        body.pack(fill="both", expand=True, padx=10, pady=(0, 10))

        # --- список игроков -------------------------------------------------
        list_frame = ttk.LabelFrame(body, text="Игроки события")
        list_frame.pack(side="left", fill="both", expand=True)

        columns = ("id", "name", "callsign", "group", "pda_uid", "registered")
        self.tree = ttk.Treeview(list_frame, columns=columns, show="headings", height=18)
        headers = {
            "id": "#", "name": "Имя", "callsign": "Позывной",
            "group": "Группа", "pda_uid": "PDA UID", "registered": "Допуск",
        }
        widths = {"id": 40, "name": 140, "callsign": 110, "group": 100,
                  "pda_uid": 120, "registered": 90}
        for c in columns:
            self.tree.heading(c, text=headers[c])
            self.tree.column(c, width=widths[c], anchor="w")
        self.tree.pack(side="left", fill="both", expand=True, padx=6, pady=6)
        self.tree.bind("<<TreeviewSelect>>", self._on_select)

        scroll = ttk.Scrollbar(list_frame, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscroll=scroll.set)
        scroll.pack(side="left", fill="y")

        # --- форма ------------------------------------------------------
        form = ttk.LabelFrame(body, text="Данные игрока")
        form.pack(side="left", fill="y", padx=(10, 0))

        self.var_name = tk.StringVar()
        self.var_callsign = tk.StringVar()
        self.var_group = tk.StringVar()
        self.var_pda_uid = tk.StringVar()
        self.var_notes = tk.StringVar()

        def row(label, var, width=24):
            r = ttk.Frame(form)
            r.pack(fill="x", padx=8, pady=4)
            ttk.Label(r, text=label, width=12).pack(side="left")
            ttk.Entry(r, textvariable=var, width=width).pack(side="left")

        row("Имя *", self.var_name)
        row("Позывной", self.var_callsign)
        row("Группа", self.var_group)
        row("PDA UID", self.var_pda_uid)
        row("Заметки", self.var_notes)

        btns = ttk.Frame(form)
        btns.pack(fill="x", padx=8, pady=(10, 4))
        ttk.Button(btns, text="Добавить", command=self._add_player).pack(fill="x", pady=2)
        ttk.Button(btns, text="Сохранить изменения", command=self._save_player).pack(fill="x", pady=2)
        ttk.Button(btns, text="Привязать PDA (вручную)", command=self._bind_pda).pack(fill="x", pady=2)
        ttk.Button(btns, text="Удалить", command=self._delete_player).pack(fill="x", pady=2)
        ttk.Button(btns, text="Очистить форму", command=self._clear_form).pack(fill="x", pady=2)

        ttk.Separator(form).pack(fill="x", padx=8, pady=8)
        ttk.Button(form, text="Экспорт CSV (игроки)", command=self._export_csv).pack(fill="x", padx=8, pady=2)

        note = ("Привязка PDA сейчас — вручную (UID).\n"
                "После основной прошивки — через слот\n"
                "и общий EEPROM-чип (см. §9.6, ROADMAP §4.2).")
        ttk.Label(form, text=note, foreground="#666", justify="left",
                  font=("Segoe UI", 8)).pack(fill="x", padx=8, pady=(10, 4))

    # ------------------------------------------------------------------

    def refresh(self):
        for i in self.tree.get_children():
            self.tree.delete(i)
        for p in self.db.list_players(self.event_id):
            self.tree.insert("", "end", iid=str(p.player_id), values=(
                p.player_id, p.name, p.callsign or "", p.group_name or "",
                p.pda_uid or "", "да" if p.pda_uid else "нет",
            ))

    def _on_select(self, _evt=None):
        sel = self.tree.selection()
        if not sel:
            return
        player_id = int(sel[0])
        p = self.db.get_player(player_id)
        if not p:
            return
        self.selected_player_id = player_id
        self.var_name.set(p.name)
        self.var_callsign.set(p.callsign or "")
        self.var_group.set(p.group_name or "")
        self.var_pda_uid.set(p.pda_uid or "")
        self.var_notes.set(p.notes or "")

    def _clear_form(self):
        self.selected_player_id = None
        for v in (self.var_name, self.var_callsign, self.var_group,
                  self.var_pda_uid, self.var_notes):
            v.set("")
        self.tree.selection_remove(self.tree.selection())

    def _add_player(self):
        name = self.var_name.get().strip()
        if not name:
            messagebox.showwarning("Регистрация", "Укажите имя игрока")
            return
        self.db.add_player(
            self.event_id, name,
            callsign=self.var_callsign.get(),
            group_name=self.var_group.get(),
            notes=self.var_notes.get(),
        )
        self._clear_form()
        self.refresh()

    def _save_player(self):
        if self.selected_player_id is None:
            messagebox.showinfo("Регистрация", "Выберите игрока в списке")
            return
        name = self.var_name.get().strip()
        if not name:
            messagebox.showwarning("Регистрация", "Имя не может быть пустым")
            return
        self.db.update_player(
            self.selected_player_id,
            name=name,
            callsign=self.var_callsign.get().strip(),
            group_name=self.var_group.get().strip(),
            notes=self.var_notes.get().strip(),
        )
        self.refresh()

    def _bind_pda(self):
        if self.selected_player_id is None:
            messagebox.showinfo("Регистрация", "Выберите игрока в списке")
            return
        uid = self.var_pda_uid.get().strip()
        if not uid:
            messagebox.showwarning("Регистрация", "Укажите PDA UID")
            return
        try:
            self.db.bind_pda(self.selected_player_id, uid, registered_by="master")
        except Exception as exc:
            messagebox.showerror("Регистрация", f"Не удалось привязать PDA: {exc}")
            return
        self.refresh()

    def _delete_player(self):
        if self.selected_player_id is None:
            messagebox.showinfo("Регистрация", "Выберите игрока в списке")
            return
        if not messagebox.askyesno("Удаление", "Удалить игрока и его статистику?"):
            return
        self.db.delete_player(self.selected_player_id)
        self._clear_form()
        self.refresh()

    def _export_csv(self):
        path = filedialog.asksaveasfilename(
            title="Экспорт списка игроков",
            defaultextension=".csv",
            filetypes=[("CSV", "*.csv")],
            initialfile="players.csv",
        )
        if not path:
            return
        self.db.export_players_csv(self.event_id, path)
        messagebox.showinfo("Экспорт", f"Сохранено: {path}")
