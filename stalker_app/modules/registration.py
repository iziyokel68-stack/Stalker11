"""
STALKER App — модуль «Регистрация» (docs/PROGRESSION.txt §10.1, §10.3 Фаза 2)
================================================================================
Список участников: ID = адрес LoRa. UID выдаёт мастер при добавлении в список.
Регистрация на ПДА — шнур к CHIP_BOX мастера через общий чип (мост).
"""

import time
import tkinter as tk
from tkinter import ttk, messagebox, filedialog

from db.event_db import EventDB
from theme import module_header


STATUS_LABEL = {
    "new": "новый",
    "registered": "зарегистрирован",
    "admitted": "допущен",
}


def _fmt_ts(ts):
    if not ts:
        return ""
    return time.strftime("%Y-%m-%d %H:%M", time.localtime(ts))


class RegistrationFrame(ttk.Frame):
    def __init__(self, master, db: EventDB, event_id: str, serial=None, on_back=None):
        super().__init__(master)
        self.db = db
        self.event_id = event_id
        self.serial = serial
        self.on_back = on_back
        self.selected_player_id = None
        self._build_ui()
        self.refresh()

    def _build_ui(self):
        module_header(self, "Регистрация и список участников", self.on_back)

        filt = ttk.Frame(self)
        filt.pack(fill="x", padx=12, pady=(0, 6))
        ttk.Label(filt, text="Поиск").pack(side="left")
        self.var_query = tk.StringVar()
        ent = ttk.Entry(filt, textvariable=self.var_query, width=28)
        ent.pack(side="left", padx=6)
        ent.bind("<KeyRelease>", lambda _e: self.refresh())
        ttk.Label(filt, text="Группа").pack(side="left", padx=(12, 0))
        self.var_group_filter = tk.StringVar()
        self.cmb_group = ttk.Combobox(filt, textvariable=self.var_group_filter,
                                      width=16, state="readonly")
        self.cmb_group.pack(side="left", padx=6)
        self.cmb_group.bind("<<ComboboxSelected>>", lambda _e: self.refresh())
        ttk.Button(filt, text="Сброс фильтра", command=self._reset_filter).pack(
            side="left", padx=6)
        self.lbl_count = ttk.Label(filt, text="", style="Dim.TLabel")
        self.lbl_count.pack(side="right")

        body = ttk.Frame(self)
        body.pack(fill="both", expand=True, padx=12, pady=(0, 10))

        list_frame = ttk.LabelFrame(body, text="Участники")
        list_frame.pack(side="left", fill="both", expand=True)

        columns = ("id", "name", "callsign", "group", "pda_uid", "status")
        self.tree = ttk.Treeview(list_frame, columns=columns, show="headings", height=18)
        headers = {
            "id": "#", "name": "Имя", "callsign": "Позывной",
            "group": "Группа", "pda_uid": "PDA UID", "status": "Статус",
        }
        widths = {"id": 40, "name": 150, "callsign": 110, "group": 100,
                  "pda_uid": 130, "status": 130}
        for c in columns:
            self.tree.heading(c, text=headers[c])
            self.tree.column(c, width=widths[c], anchor="w")
        self.tree.pack(side="left", fill="both", expand=True, padx=6, pady=6)
        self.tree.bind("<<TreeviewSelect>>", self._on_select)
        scroll = ttk.Scrollbar(list_frame, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscroll=scroll.set)
        scroll.pack(side="left", fill="y")

        form = ttk.LabelFrame(body, text="Карточка игрока")
        form.pack(side="left", fill="y", padx=(10, 0))

        self.var_name = tk.StringVar()
        self.var_callsign = tk.StringVar()
        self.var_group = tk.StringVar()
        self.var_pda_uid = tk.StringVar()
        self.var_notes = tk.StringVar()

        def row(label, var, width=26):
            r = ttk.Frame(form)
            r.pack(fill="x", padx=8, pady=4)
            ttk.Label(r, text=label, width=12).pack(side="left")
            ttk.Entry(r, textvariable=var, width=width).pack(side="left")

        row("Имя *", self.var_name)
        row("Позывной", self.var_callsign)
        row("Группа", self.var_group)
        row("Заметки", self.var_notes)
        ttk.Label(form, text="PDA UID (выдаёт мастер, не считывается с ПДА)",
                  style="Dim.TLabel").pack(anchor="w", padx=8)
        ttk.Entry(form, textvariable=self.var_pda_uid, width=26,
                  state="readonly").pack(anchor="w", padx=8, pady=(0, 4))
        self.lbl_status = ttk.Label(form, text="Статус: новый", style="Dim.TLabel")
        self.lbl_status.pack(anchor="w", padx=8, pady=(4, 8))

        ttk.Button(form, text="Добавить в список", command=self._add_player).pack(
            fill="x", padx=8, pady=2)
        ttk.Button(form, text="Сохранить изменения", command=self._save_player).pack(
            fill="x", padx=8, pady=2)
        ttk.Button(form, text="Записать на мост (шнур к ПДА)", style="Accent.TButton",
                   command=self._write_bridge).pack(fill="x", padx=8, pady=(10, 2))
        ttk.Button(form, text="Проверить ответ ПДА", command=self._poll_bridge).pack(
            fill="x", padx=8, pady=2)
        ttk.Button(form, text="Прошить чип допуска", command=self._write_admit_chip).pack(
            fill="x", padx=8, pady=2)
        ttk.Button(form, text="Допуск по LoRa (ID)", command=self._admit_lora).pack(
            fill="x", padx=8, pady=2)
        ttk.Button(form, text="Допуск только в базе", command=self._admit_db).pack(
            fill="x", padx=8, pady=2)
        ttk.Button(form, text="Удалить", style="Danger.TButton",
                   command=self._delete_player).pack(fill="x", padx=8, pady=(10, 2))
        ttk.Button(form, text="Очистить форму", command=self._clear_form).pack(
            fill="x", padx=8, pady=2)

        ttk.Separator(form).pack(fill="x", padx=8, pady=8)
        ttk.Button(form, text="Экспорт CSV (игроки)", command=self._export_csv).pack(
            fill="x", padx=8, pady=2)

        note = (
            "ID в списке = адрес LoRa (0 = все).\n"
            "UID выдаётся при добавлении игрока.\n"
            "Регистрация: CHIP_BOX мастера + общий чип +\n"
            "шнур к ПДА. Имена в LoRa не ходят."
        )
        ttk.Label(form, text=note, style="Dim.TLabel", justify="left").pack(
            fill="x", padx=8, pady=(10, 8))

    def _reset_filter(self):
        self.var_query.set("")
        self.var_group_filter.set("")
        self.refresh()

    def refresh(self):
        groups = [""] + self.db.list_groups(self.event_id)
        self.cmb_group["values"] = groups
        for i in self.tree.get_children():
            self.tree.delete(i)
        players = self.db.list_players(
            self.event_id,
            query=self.var_query.get(),
            group_name=self.var_group_filter.get(),
        )
        for p in players:
            self.tree.insert("", "end", iid=str(p.player_id), values=(
                p.player_id, p.name, p.callsign or "", p.group_name or "",
                p.pda_uid or "", STATUS_LABEL.get(p.status, p.status),
            ))
        counts = self.db.player_counts(self.event_id)
        self.lbl_count.configure(
            text=f"всего {counts['total']}  ·  рег. {counts['registered']}  ·  "
                 f"допуск {counts['admitted']}"
        )

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
        extra = []
        if p.registered_at:
            extra.append("рег. " + _fmt_ts(p.registered_at))
        if p.admitted_at:
            extra.append("допуск " + _fmt_ts(p.admitted_at))
        self.lbl_status.configure(
            text="Статус: " + STATUS_LABEL.get(p.status, p.status)
            + (("  ·  " + ", ".join(extra)) if extra else "")
        )

    def _clear_form(self):
        self.selected_player_id = None
        for v in (self.var_name, self.var_callsign, self.var_group,
                  self.var_pda_uid, self.var_notes):
            v.set("")
        self.lbl_status.configure(text="Статус: новый")
        self.tree.selection_remove(self.tree.selection())

    def _add_player(self):
        name = self.var_name.get().strip()
        if not name:
            messagebox.showwarning("Регистрация", "Укажите имя игрока", parent=self)
            return
        pid = self.db.add_player(
            self.event_id, name,
            callsign=self.var_callsign.get(),
            group_name=self.var_group.get(),
            notes=self.var_notes.get(),
        )
        p = self.db.get_player(pid)
        self._clear_form()
        self.refresh()
        messagebox.showinfo(
            "Регистрация",
            f"Игрок добавлен. ID {pid}, UID {p.pda_uid if p else ''}.\n"
            "UID выдан мастером — на ПДА он попадёт через мост.",
            parent=self,
        )

    def _save_player(self):
        if self.selected_player_id is None:
            messagebox.showinfo("Регистрация", "Выберите игрока в списке", parent=self)
            return
        name = self.var_name.get().strip()
        if not name:
            messagebox.showwarning("Регистрация", "Имя не может быть пустым", parent=self)
            return
        self.db.update_player(
            self.selected_player_id,
            name=name,
            callsign=self.var_callsign.get().strip(),
            group_name=self.var_group.get().strip(),
            notes=self.var_notes.get().strip(),
        )
        self.refresh()

    def _write_bridge(self):
        if self.selected_player_id is None:
            messagebox.showinfo("Регистрация", "Выберите игрока в списке", parent=self)
            return
        if self.serial is None:
            messagebox.showwarning("Регистрация", "Serial-сессия недоступна", parent=self)
            return
        p = self.db.get_player(self.selected_player_id)
        ok, resp = self.serial.register_bridge(p.player_id, p.name, p.pda_uid or "")
        if not ok:
            messagebox.showerror(
                "Регистрация",
                "Мост не записан (нужен CHIP_BOX с общим чипом).\n"
                f"{resp}",
                parent=self,
            )
            return
        self.db.mark_registered(self.selected_player_id, registered_by="bridge")
        self.refresh()
        self._on_select()
        messagebox.showinfo(
            "Регистрация",
            f"Мост ID {p.player_id}, UID {p.pda_uid}.\n"
            "Подключите ПДА шнуром к CHIP_BOX — он заберёт ID и UID.\n"
            f"{resp}",
            parent=self,
        )

    def _poll_bridge(self):
        if self.serial is None:
            return
        status, msg = self.serial.poll_txn()
        if status is None:
            messagebox.showwarning("Мост", str(msg), parent=self)
            return
        messagebox.showinfo("Мост", str(status), parent=self)

    def _write_admit_chip(self):
        if self.selected_player_id is None:
            messagebox.showinfo("Регистрация", "Выберите игрока в списке", parent=self)
            return
        if self.serial is None:
            messagebox.showwarning("Регистрация", "Serial-сессия недоступна", parent=self)
            return
        ok, resp = self.serial.write_admit_chip()
        if not ok:
            messagebox.showerror("Допуск", f"Чип допуска не записан:\n{resp}", parent=self)
            return
        self.db.mark_admitted(self.selected_player_id, admitted_by="eeprom")
        self.refresh()
        self._on_select()
        messagebox.showinfo("Допуск", f"Чип допуска готов.\n{resp}", parent=self)

    def _admit_lora(self):
        if self.selected_player_id is None:
            messagebox.showinfo("Регистрация", "Выберите игрока в списке", parent=self)
            return
        if self.serial is None:
            messagebox.showwarning("Регистрация", "Serial-сессия недоступна", parent=self)
            return
        ok, resp = self.serial.admit_lora(self.selected_player_id)
        if ok:
            self.db.mark_admitted(self.selected_player_id, admitted_by="lora")
            self.refresh()
            self._on_select()
            messagebox.showinfo("Допуск", f"LoRa на № {self.selected_player_id}.\n{resp}",
                                parent=self)
        else:
            messagebox.showwarning("Допуск", str(resp), parent=self)

    def _admit_db(self):
        if self.selected_player_id is None:
            messagebox.showinfo("Регистрация", "Выберите игрока в списке", parent=self)
            return
        self.db.mark_admitted(self.selected_player_id, admitted_by="master")
        self.refresh()
        self._on_select()

    def _delete_player(self):
        if self.selected_player_id is None:
            messagebox.showinfo("Регистрация", "Выберите игрока в списке", parent=self)
            return
        if not messagebox.askyesno("Удаление", "Удалить игрока и его статистику?",
                                   parent=self):
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
            parent=self,
        )
        if not path:
            return
        self.db.export_players_csv(self.event_id, path)
        messagebox.showinfo("Экспорт", f"Сохранено: {path}", parent=self)
