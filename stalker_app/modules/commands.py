"""Команды мастера на подключённый ПДА: допуск, воскрешение, чтение снимка."""

import json
import tkinter as tk
from tkinter import ttk, messagebox

from theme import module_header


class CommandsFrame(ttk.Frame):
    def __init__(self, master, db, event_id, serial=None, on_back=None):
        super().__init__(master)
        self.db = db
        self.event_id = event_id
        self.serial = serial
        self.on_back = on_back
        self._build()

    def _build(self):
        module_header(self, "Команды мастера", self.on_back)
        body = ttk.Frame(self)
        body.pack(fill="both", expand=True, padx=12, pady=(0, 12))

        ttk.Label(
            body,
            text="USB Serial к ПДА. LoRa-рассылка KILL/REVIVE/WIPE на поле — Мастер-Пульт (фаза 5).",
            style="Dim.TLabel",
        ).pack(anchor="w", pady=(0, 10))

        row = ttk.Frame(body)
        row.pack(fill="x", pady=4)
        ttk.Button(row, text="Допуск (CONFIG:ADMIT)", style="Accent.TButton",
                   command=self._admit).pack(side="left", padx=4)
        ttk.Button(row, text="Воскрешение (CONFIG:REVIVE)",
                   command=self._revive).pack(side="left", padx=4)
        ttk.Button(row, text="Считать UID", command=self._uid).pack(side="left", padx=4)
        ttk.Button(row, text="Снимок CONFIG_READ", command=self._snap).pack(
            side="left", padx=4)

        self.out = tk.Text(body, height=18, bg="#252732", fg="#c8f0d0",
                           insertbackground="#e6e6ea", borderwidth=0, font=("DejaVu Sans Mono", 10))
        self.out.pack(fill="both", expand=True, pady=(10, 0))

        ttk.Label(
            body,
            text="ADMIT ≠ REVIVE. Допуск — каждое включение, только главный мастер.\n"
                 "Воскрешение — при «СВЯЗЬ ПОТЕРЯНА», не для зомби.",
            style="Dim.TLabel", justify="left",
        ).pack(anchor="w", pady=(8, 0))

    def _log(self, text):
        self.out.insert("end", text + "\n")
        self.out.see("end")

    def _admit(self):
        if not self.serial:
            return
        ok, resp = self.serial.admit()
        self._log(("OK " if ok else "FAIL ") + str(resp))
        if not ok:
            messagebox.showerror("Допуск", str(resp), parent=self)

    def _revive(self):
        if not self.serial:
            return
        ok, resp = self.serial.revive()
        self._log(("OK " if ok else "FAIL ") + str(resp))
        if not ok:
            messagebox.showerror("Воскрешение", str(resp), parent=self)

    def _uid(self):
        if not self.serial:
            return
        uid, msg = self.serial.read_uid()
        self._log(f"UID: {uid or '—'}  ({msg})")

    def _snap(self):
        if not self.serial:
            return
        snap, msg = self.serial.read_snapshot()
        if not snap:
            self._log("FAIL " + str(msg))
            return
        self._log(json.dumps(snap, ensure_ascii=False, indent=2, default=str))
